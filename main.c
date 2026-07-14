/**
 * @file main.c
 * @brief BME680 Environmental Sensor - Iteration 5: Persistent baseline
 *
 * Builds on iter-4's IAQ index by persisting the gas-resistance baseline to
 * the Pico's on-board flash. On boot the stored baseline is restored (if it
 * passes a 7-day freshness check) so the IAQ index is stable from the first
 * valid reading instead of re-warming from scratch every power cycle.
 *
 * Wall-clock time (needed for the freshness check) is injected over UART: the
 * test harness sends a "TIME <unix_seconds>" line, which the firmware reads on
 * stdin. Without it the firmware still restores the baseline but cannot age it.
 *
 * Hardware:
 * - Raspberry Pi Pico (RP2040)
 * - Bosch BME680 sensor on I2C0 (GP4=SDA, GP5=SCL)
 * - I2C address: 0x77 (SDO high/floating) or 0x76 (SDO low)
 * - Pico Debug Probe for SWD debugging and UART capture
 *
 * Output: UART0 at 115200 baud (captured by debug probe)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "bme680.h"
#include "flash_store.h"

// I2C Configuration
#define I2C_PORT        i2c0
#define I2C_SDA_PIN     4
#define I2C_SCL_PIN     5
#define I2C_FREQ_HZ     100000  // 100 kHz standard mode

// BME680 I2C address (adjust if SDO is tied to GND)
#define BME680_ADDR     BME680_I2C_ADDR_PRIMARY  // 0x77

// Onboard LED pin
#define LED_PIN         25

// Measurement interval (5 seconds for iteration 1)
#define MEASURE_INTERVAL_MS     5000

// IAQ baseline window: max gas resistance over the last N readings.
// At 5 s interval, 60 readings = 5 minutes of warm-up before the
// baseline is considered fully populated.
#define IAQ_WINDOW_SIZE         60

// Optimal humidity for IAQ scoring (centred on 40 %RH).
#define IAQ_HUM_OPTIMAL         40.0f

// --- Iteration 5: persistent baseline ---
// A restored baseline older than this is discarded and re-warmed.
#define BASELINE_MAX_AGE_S          (7u * 24u * 3600u)   // 7 days
// Minimum gas samples before the running-max baseline is worth persisting.
#define BASELINE_MIN_SAMPLES        10u
// Don't rewrite flash if the baseline moved by less than this fraction (1/10).
#define BASELINE_CHANGE_DIV         10u
// Rate-limit flash writes so a busy loop can't thrash the sector.
#define BASELINE_SAVE_INTERVAL_MS   30000u

// Statistics tracking
typedef struct {
    uint32_t total_reads;
    uint32_t total_fails;
    float temp_sum;
    float temp_sq_sum;
    float hum_sum;
    float hum_sq_sum;
    float press_sum;
    float press_sq_sum;
    uint64_t gas_sum;
    uint64_t gas_sq_sum;
    float iaq_sum;
    float iaq_sq_sum;
    uint32_t iaq_count;
    uint32_t first_valid_ms;
    bool first_valid_recorded;
} stats_t;

static stats_t stats = {0};
static bme680_dev_t sensor;

// Gas resistance ring buffer used to derive the IAQ baseline (running max).
static uint32_t gas_history[IAQ_WINDOW_SIZE] = {0};
static uint32_t gas_history_count = 0;
static uint32_t gas_history_idx = 0;

static void gas_history_push(uint32_t gas) {
    gas_history[gas_history_idx] = gas;
    gas_history_idx = (gas_history_idx + 1) % IAQ_WINDOW_SIZE;
    if (gas_history_count < IAQ_WINDOW_SIZE) gas_history_count++;
}

static uint32_t gas_baseline_max(void) {
    uint32_t max = 0;
    for (uint32_t i = 0; i < gas_history_count; i++) {
        if (gas_history[i] > max) max = gas_history[i];
    }
    return max;
}

// --- Iteration 5: persistent baseline + injected wall-clock ---

// Baseline restored from / persisted to flash.
static uint32_t persisted_baseline = 0;     // 0 = nothing persisted yet
static bool     baseline_from_flash = false;
static uint32_t stored_saved_epoch = 0;     // saved_epoch of the restored record
static uint32_t stored_boot_count = 0;
static uint32_t last_save_ms = 0;
static bool     restored_at_boot = false;   // baseline came from flash at boot
static bool     freshness_checked = false;

// Wall-clock injected over UART as "TIME <unix_seconds>". We hold the epoch and
// the boot-relative timestamp it arrived at, then extrapolate with uptime.
static uint32_t wallclock_epoch_ref = 0;
static uint32_t wallclock_boot_ms = 0;
static bool     wallclock_known = false;

// Current unix time, or 0 if no TIME line has been received yet.
static uint32_t wallclock_now(void) {
    if (!wallclock_known) return 0;
    uint32_t up = to_ms_since_boot(get_absolute_time());
    return wallclock_epoch_ref + (up - wallclock_boot_ms) / 1000u;
}

// Non-blocking: drain UART stdin and act on any complete "TIME <epoch>" line.
static void poll_time_injection(void) {
    static char buf[32];
    static uint32_t len = 0;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            if (len > 5 && strncmp(buf, "TIME ", 5) == 0) {
                uint32_t epoch = (uint32_t)strtoul(buf + 5, NULL, 10);
                if (epoch > 0) {
                    wallclock_epoch_ref = epoch;
                    wallclock_boot_ms = to_ms_since_boot(get_absolute_time());
                    wallclock_known = true;
                    printf("[INFO] wall-clock set: epoch=%lu\n", (unsigned long)epoch);
                }
            }
            len = 0;
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = (char)c;
        } else {
            len = 0;  // overrun: drop the partial line
        }
    }
}

// Restore the persisted baseline at boot: seed the ring buffer so the running
// max returns the stored value and warming_up clears immediately.
static void seed_baseline_from_flash(void) {
    flash_store_t rec;
    if (!flash_store_load(&rec)) {
        printf("[BASELINE] no valid record in flash; cold start\n");
        return;
    }
    if (rec.gas_baseline == 0) {
        printf("[BASELINE] stored baseline is zero; ignoring\n");
        return;
    }

    persisted_baseline = rec.gas_baseline;
    baseline_from_flash = true;
    restored_at_boot = true;
    stored_saved_epoch = rec.saved_epoch;
    stored_boot_count = rec.boot_count;

    for (uint32_t i = 0; i < IAQ_WINDOW_SIZE; i++) gas_history[i] = persisted_baseline;
    gas_history_count = IAQ_WINDOW_SIZE;
    gas_history_idx = 0;

    printf("[BASELINE] restored from flash: gas=%lu ohms saved_epoch=%lu boot_count=%lu\n",
           (unsigned long)rec.gas_baseline,
           (unsigned long)rec.saved_epoch,
           (unsigned long)rec.boot_count);
}

// Once wall-clock time is known, discard the restored baseline if it is stale.
// Runs at most once (freshness_checked latches).
static void maybe_invalidate_stale_baseline(void) {
    if (freshness_checked || !restored_at_boot || !wallclock_known) return;
    freshness_checked = true;

    if (stored_saved_epoch == 0) {
        printf("[BASELINE] restored baseline has no timestamp; keeping (age unknown)\n");
        return;
    }
    uint32_t now = wallclock_now();
    if (now < stored_saved_epoch) {
        printf("[BASELINE] clock precedes saved time; keeping restored baseline\n");
        return;
    }
    uint32_t age = now - stored_saved_epoch;
    if (age > BASELINE_MAX_AGE_S) {
        printf("[BASELINE] stale (age=%lu s > %lu s); discarding, re-warming\n",
               (unsigned long)age, (unsigned long)BASELINE_MAX_AGE_S);
        for (uint32_t i = 0; i < IAQ_WINDOW_SIZE; i++) gas_history[i] = 0;
        gas_history_count = 0;
        gas_history_idx = 0;
        baseline_from_flash = false;
        restored_at_boot = false;
        persisted_baseline = 0;
    } else {
        printf("[BASELINE] fresh (age=%lu s); keeping restored baseline\n",
               (unsigned long)age);
    }
}

// Persist the current running-max baseline when it is meaningful and has moved.
static void maybe_save_baseline(uint32_t current_baseline) {
    if (current_baseline == 0) return;
    if (gas_history_count < BASELINE_MIN_SAMPLES) return;  // not enough evidence yet

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (last_save_ms != 0 && (now_ms - last_save_ms) < BASELINE_SAVE_INTERVAL_MS) return;

    // Only rewrite when nothing is stored yet or the baseline moved > 10 %.
    bool worth = !baseline_from_flash;
    if (persisted_baseline > 0) {
        uint32_t delta = persisted_baseline / BASELINE_CHANGE_DIV;
        if (current_baseline > persisted_baseline + delta ||
            current_baseline < persisted_baseline - delta) {
            worth = true;
        }
    }
    if (!worth) return;

    flash_store_t rec = {0};
    rec.gas_baseline = current_baseline;
    rec.saved_epoch = wallclock_now();       // 0 if wall-clock still unknown
    rec.boot_count = stored_boot_count + 1;

    if (flash_store_save(&rec)) {
        persisted_baseline = current_baseline;
        baseline_from_flash = true;
        stored_saved_epoch = rec.saved_epoch;
        stored_boot_count = rec.boot_count;
        last_save_ms = now_ms;
        printf("[BASELINE] saved to flash: gas=%lu ohms epoch=%lu boot_count=%lu\n",
               (unsigned long)current_baseline,
               (unsigned long)rec.saved_epoch,
               (unsigned long)rec.boot_count);
    } else {
        printf("[BASELINE] flash save FAILED (verify mismatch)\n");
    }
}

// IAQ score: 0 (excellent) to 500 (hazardous).
// Gas component is 75 % of the total air-quality score (mapped from
// gas/baseline ratio), humidity component is 25 % (best at 40 %RH).
static float compute_iaq(uint32_t gas_resistance, uint32_t baseline, float humidity) {
    if (baseline == 0) return 0.0f;

    float gas_ratio = (float)gas_resistance / (float)baseline;
    if (gas_ratio > 1.0f) gas_ratio = 1.0f;
    float gas_score = gas_ratio * 75.0f;

    float hum_offset = humidity - IAQ_HUM_OPTIMAL;
    float hum_score;
    if (hum_offset >= 0.0f) {
        hum_score = ((100.0f - IAQ_HUM_OPTIMAL - hum_offset) / (100.0f - IAQ_HUM_OPTIMAL)) * 25.0f;
    } else {
        hum_score = ((IAQ_HUM_OPTIMAL + hum_offset) / IAQ_HUM_OPTIMAL) * 25.0f;
    }
    if (hum_score < 0.0f) hum_score = 0.0f;
    if (hum_score > 25.0f) hum_score = 25.0f;

    float air_quality_score = gas_score + hum_score;
    float iaq = (100.0f - air_quality_score) * 5.0f;
    if (iaq < 0.0f) iaq = 0.0f;
    if (iaq > 500.0f) iaq = 500.0f;
    return iaq;
}

/**
 * @brief Initialize I2C peripheral
 */
static void init_i2c(void) {
    i2c_init(I2C_PORT, I2C_FREQ_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

/**
 * @brief Initialize LED
 */
static void init_led(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

/**
 * @brief Calculate standard deviation
 */
static float calculate_stddev(float sum, float sq_sum, uint32_t count) {
    if (count < 2) return 0.0f;
    float mean = sum / count;
    float variance = (sq_sum / count) - (mean * mean);
    return (variance > 0) ? sqrtf(variance) : 0.0f;
}

/**
 * @brief Print summary statistics
 */
static void print_summary(void) {
    float fail_rate = 0.0f;
    if (stats.total_reads + stats.total_fails > 0) {
        fail_rate = (float)stats.total_fails / (stats.total_reads + stats.total_fails) * 100.0f;
    }

    float temp_mean = (stats.total_reads > 0) ? stats.temp_sum / stats.total_reads : 0.0f;
    float temp_stddev = calculate_stddev(stats.temp_sum, stats.temp_sq_sum, stats.total_reads);

    float hum_mean = (stats.total_reads > 0) ? stats.hum_sum / stats.total_reads : 0.0f;
    float hum_stddev = calculate_stddev(stats.hum_sum, stats.hum_sq_sum, stats.total_reads);

    float press_mean = (stats.total_reads > 0) ? stats.press_sum / stats.total_reads : 0.0f;
    float press_stddev = calculate_stddev(stats.press_sum, stats.press_sq_sum, stats.total_reads);

    float gas_mean = (stats.total_reads > 0) ? (float)stats.gas_sum / stats.total_reads : 0.0f;
    // For gas stddev, need to handle large numbers carefully
    float gas_variance = 0.0f;
    if (stats.total_reads > 1) {
        float gas_mean_sq = gas_mean * gas_mean;
        float gas_sq_mean = (float)stats.gas_sq_sum / stats.total_reads;
        gas_variance = gas_sq_mean - gas_mean_sq;
    }
    float gas_stddev = (gas_variance > 0) ? sqrtf(gas_variance) : 0.0f;

    float iaq_mean = (stats.iaq_count > 0) ? stats.iaq_sum / stats.iaq_count : 0.0f;
    float iaq_stddev = 0.0f;
    if (stats.iaq_count > 1) {
        float iaq_variance = (stats.iaq_sq_sum / stats.iaq_count) - (iaq_mean * iaq_mean);
        iaq_stddev = (iaq_variance > 0.0f) ? sqrtf(iaq_variance) : 0.0f;
    }

    uint32_t uptime_s = to_ms_since_boot(get_absolute_time()) / 1000;

    printf("[SUMMARY] reads=%lu fails=%lu fail_rate=%.2f "
           "temp_mean=%.1f temp_stddev=%.1f hum_mean=%.1f hum_stddev=%.1f "
           "press_mean=%.0f press_stddev=%.1f gas_mean=%.0f gas_stddev=%.0f "
           "iaq_mean=%.1f iaq_stddev=%.1f "
           "first_valid_ms=%lu uptime_s=%lu\n",
           stats.total_reads, stats.total_fails, fail_rate,
           temp_mean, temp_stddev, hum_mean, hum_stddev,
           press_mean, press_stddev, gas_mean, gas_stddev,
           iaq_mean, iaq_stddev,
           stats.first_valid_ms, uptime_s);
}

int main(void) {
    // Initialize stdio for UART output
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB/UART to stabilize

    printf("\n");
    printf("========================================\n");
    printf("BME680 Environmental Sensor - Iteration 5\n");
    printf("========================================\n");
    printf("[INFO] Persistent gas baseline (flash-backed IAQ)\n");
    printf("[INFO] I2C: GP%d (SDA), GP%d (SCL), %d Hz\n", I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
    printf("[INFO] BME680 address: 0x%02X\n", BME680_ADDR);
    printf("\n");

    // Initialize hardware
    init_i2c();
    init_led();

    // Initialize BME680
    printf("[INFO] Initializing BME680...\n");
    bme680_error_t err = bme680_init(&sensor, I2C_PORT, BME680_ADDR);
    if (err != BME680_OK) {
        printf("[ERROR] BME680 init failed: %s\n", bme680_error_string(err));
        while (1) {
            gpio_put(LED_PIN, 1);
            sleep_ms(100);
            gpio_put(LED_PIN, 0);
            sleep_ms(100);
        }
    }

    printf("[INFO] BME680 initialized successfully!\n");
    printf("[INFO] Heater: %d°C, %d ms\n", sensor.heater_temp, sensor.heater_dur);
    printf("[INFO] Measurement interval: %d ms\n", MEASURE_INTERVAL_MS);

    // Restore the persisted gas baseline before the first measurement so IAQ is
    // meaningful from reading #1. Freshness is validated once wall-clock arrives.
    seed_baseline_from_flash();

    printf("\n");
    printf("Starting measurements...\n");
    printf("\n");

    absolute_time_t last_measurement = get_absolute_time();
    absolute_time_t last_summary = get_absolute_time();
    uint32_t measurement_count = 0;

    while (true) {
        absolute_time_t now = get_absolute_time();

        // Accept an injected wall-clock and, once known, age-check the baseline.
        poll_time_injection();
        maybe_invalidate_stale_baseline();

        // Trigger measurement every MEASURE_INTERVAL_MS
        if (absolute_time_diff_us(last_measurement, now) >= MEASURE_INTERVAL_MS * 1000) {
            last_measurement = now;
            measurement_count++;

            // Blink LED to show activity
            gpio_put(LED_PIN, 1);

            // Trigger forced mode measurement
            err = bme680_trigger_measurement(&sensor);
            if (err != BME680_OK) {
                printf("[METRIC] read_fail=1 err=%s ts_ms=%lu\n",
                       bme680_error_string(err),
                       to_ms_since_boot(get_absolute_time()));
                stats.total_fails++;
                gpio_put(LED_PIN, 0);
                continue;
            }

            // Wait for measurement to complete (typical: 200-300ms)
            sleep_ms(250);

            // Read measurement data
            bme680_data_t data;
            err = bme680_read_data(&sensor, &data);
            gpio_put(LED_PIN, 0);

            if (err != BME680_OK) {
                printf("[METRIC] read_fail=1 err=%s ts_ms=%lu\n",
                       bme680_error_string(err),
                       to_ms_since_boot(get_absolute_time()));
                stats.total_fails++;
                continue;
            }

            // Record first valid measurement timestamp
            if (!stats.first_valid_recorded && data.gas_valid) {
                stats.first_valid_ms = to_ms_since_boot(get_absolute_time());
                stats.first_valid_recorded = true;
            }

            // Update statistics
            stats.total_reads++;
            stats.temp_sum += data.temperature;
            stats.temp_sq_sum += data.temperature * data.temperature;
            stats.hum_sum += data.humidity;
            stats.hum_sq_sum += data.humidity * data.humidity;
            stats.press_sum += data.pressure;
            stats.press_sq_sum += data.pressure * data.pressure;
            stats.gas_sum += data.gas_resistance;
            stats.gas_sq_sum += (uint64_t)data.gas_resistance * data.gas_resistance;

            // Derive IAQ from gas baseline + humidity
            float iaq = 0.0f;
            uint32_t baseline = 0;
            bool warming_up = true;
            if (data.gas_valid) {
                gas_history_push(data.gas_resistance);
                baseline = gas_baseline_max();
                iaq = compute_iaq(data.gas_resistance, baseline, data.humidity);
                warming_up = (gas_history_count < IAQ_WINDOW_SIZE);
                stats.iaq_sum += iaq;
                stats.iaq_sq_sum += iaq * iaq;
                stats.iaq_count++;

                // Persist the baseline so the next boot starts warm.
                maybe_save_baseline(baseline);
            }

            // Print metric line
            printf("[METRIC] read_ok=1 temp=%.1f hum=%.1f press=%.0f gas=%lu "
                   "gas_valid=%d iaq=%.1f iaq_baseline=%lu warming_up=%d ts_ms=%lu\n",
                   data.temperature, data.humidity, data.pressure,
                   data.gas_resistance, data.gas_valid,
                   iaq, baseline, warming_up ? 1 : 0,
                   to_ms_since_boot(get_absolute_time()));
        }

        // Print summary every 10 seconds
        if (absolute_time_diff_us(last_summary, now) >= 10000000) {
            last_summary = now;
            print_summary();
        }

        sleep_ms(100);
    }

    return 0;
}
