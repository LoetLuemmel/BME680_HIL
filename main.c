/**
 * @file main.c
 * @brief BME680 Environmental Sensor - Iteration 1: Naive Driver
 *
 * This is the baseline implementation with minimal error handling.
 * Outputs structured metrics for automated testing.
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
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "bme680.h"

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
    uint32_t first_valid_ms;
    bool first_valid_recorded;
} stats_t;

static stats_t stats = {0};
static bme680_dev_t sensor;

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

    uint32_t uptime_s = to_ms_since_boot(get_absolute_time()) / 1000;

    printf("[SUMMARY] reads=%lu fails=%lu fail_rate=%.2f "
           "temp_mean=%.1f temp_stddev=%.1f hum_mean=%.1f hum_stddev=%.1f "
           "press_mean=%.0f press_stddev=%.1f gas_mean=%.0f gas_stddev=%.0f "
           "first_valid_ms=%lu uptime_s=%lu\n",
           stats.total_reads, stats.total_fails, fail_rate,
           temp_mean, temp_stddev, hum_mean, hum_stddev,
           press_mean, press_stddev, gas_mean, gas_stddev,
           stats.first_valid_ms, uptime_s);
}

int main(void) {
    // Initialize stdio for UART output
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB/UART to stabilize

    printf("\n");
    printf("========================================\n");
    printf("BME680 Environmental Sensor - Iteration 1\n");
    printf("========================================\n");
    printf("[INFO] Naive driver - baseline implementation\n");
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
    printf("\n");
    printf("Starting measurements...\n");
    printf("\n");

    absolute_time_t last_measurement = get_absolute_time();
    absolute_time_t last_summary = get_absolute_time();
    uint32_t measurement_count = 0;

    while (true) {
        absolute_time_t now = get_absolute_time();

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

            // Print metric line
            printf("[METRIC] read_ok=1 temp=%.1f hum=%.1f press=%.0f gas=%lu "
                   "gas_valid=%d ts_ms=%lu\n",
                   data.temperature, data.humidity, data.pressure,
                   data.gas_resistance, data.gas_valid,
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
