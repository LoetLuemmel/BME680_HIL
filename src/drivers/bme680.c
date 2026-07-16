/**
 * @file bme680.c
 * @brief BME680 driver implementation - Iteration 1 (naive driver)
 *
 * This is the baseline implementation with minimal error handling.
 * Future iterations will add:
 * - Robust I2C with retry (iter-2)
 * - Proper gas sensor heater configuration (iter-3)
 * - Signal conditioning and filtering (iter-4)
 * - Calibration and baseline management (iter-5)
 * - Power optimization (iter-6)
 * - Diagnostics and self-test (iter-7)
 */

#include "bme680.h"
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"

// Helper functions for I2C communication

static int bme680_write_reg(bme680_dev_t *dev, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_write_blocking(dev->i2c_port, dev->i2c_addr, buf, 2, false);
}

static int bme680_read_reg(bme680_dev_t *dev, uint8_t reg, uint8_t *value) {
    int ret = i2c_write_blocking(dev->i2c_port, dev->i2c_addr, &reg, 1, true);
    if (ret < 0) return ret;
    return i2c_read_blocking(dev->i2c_port, dev->i2c_addr, value, 1, false);
}

static int bme680_read_regs(bme680_dev_t *dev, uint8_t reg, uint8_t *buf, uint8_t len) {
    int ret = i2c_write_blocking(dev->i2c_port, dev->i2c_addr, &reg, 1, true);
    if (ret < 0) return ret;
    return i2c_read_blocking(dev->i2c_port, dev->i2c_addr, buf, len, false);
}

// Load calibration coefficients from sensor
static bme680_error_t bme680_load_calibration(bme680_dev_t *dev) {
    uint8_t coeff_buf1[25];
    uint8_t coeff_buf2[16];

    // Read calibration block 1
    if (bme680_read_regs(dev, BME680_REG_COEFF_1_START, coeff_buf1, 25) < 0) {
        return BME680_ERR_I2C;
    }

    // Read calibration block 2
    if (bme680_read_regs(dev, BME680_REG_COEFF_2_START, coeff_buf2, 16) < 0) {
        return BME680_ERR_I2C;
    }

    // Parse temperature calibration (par_t1 at 0xE9/0xEA, par_t2 at 0x8A/0x8B,
    // par_t3 at 0x8C — buf1 starts at 0x89 so register 0xXX is at buf1[0xXX-0x89])
    dev->calib.par_t1 = (uint16_t)(coeff_buf2[9] << 8) | coeff_buf2[8];
    dev->calib.par_t2 = (int16_t)(coeff_buf1[2] << 8) | coeff_buf1[1];
    dev->calib.par_t3 = (int8_t)coeff_buf1[3];

    // Parse pressure calibration
    dev->calib.par_p1 = (uint16_t)(coeff_buf1[6] << 8) | coeff_buf1[5];
    dev->calib.par_p2 = (int16_t)(coeff_buf1[8] << 8) | coeff_buf1[7];
    dev->calib.par_p3 = (int8_t)coeff_buf1[9];
    dev->calib.par_p4 = (int16_t)(coeff_buf1[12] << 8) | coeff_buf1[11];
    dev->calib.par_p5 = (int16_t)(coeff_buf1[14] << 8) | coeff_buf1[13];
    dev->calib.par_p6 = (int8_t)coeff_buf1[16];
    dev->calib.par_p7 = (int8_t)coeff_buf1[15];
    dev->calib.par_p8 = (int16_t)(coeff_buf1[20] << 8) | coeff_buf1[19];
    dev->calib.par_p9 = (int16_t)(coeff_buf1[22] << 8) | coeff_buf1[21];
    dev->calib.par_p10 = (uint8_t)coeff_buf1[23];

    // Parse humidity calibration (par_h1 spans 0xE2/0xE3, par_h2 spans 0xE1/0xE2)
    dev->calib.par_h1 = (uint16_t)(coeff_buf2[2] << 4) | (coeff_buf2[1] & 0x0F);
    dev->calib.par_h2 = (uint16_t)(coeff_buf2[0] << 4) | (coeff_buf2[1] >> 4);
    dev->calib.par_h3 = (int8_t)coeff_buf2[3];
    dev->calib.par_h4 = (int8_t)coeff_buf2[4];
    dev->calib.par_h5 = (int8_t)coeff_buf2[5];
    dev->calib.par_h6 = (uint8_t)coeff_buf2[6];
    dev->calib.par_h7 = (int8_t)coeff_buf2[7];

    // Parse gas sensor calibration
    dev->calib.par_g1 = (int8_t)coeff_buf2[12];
    dev->calib.par_g2 = (int16_t)(coeff_buf2[11] << 8) | coeff_buf2[10];
    dev->calib.par_g3 = (int8_t)coeff_buf2[13];

    // Read heater calibration values
    uint8_t res_heat_range, res_heat_val, range_sw_err;
    bme680_read_reg(dev, 0x02, &res_heat_range);
    bme680_read_reg(dev, 0x00, &res_heat_val);
    bme680_read_reg(dev, 0x04, &range_sw_err);

    dev->calib.res_heat_range = (res_heat_range & 0x30) >> 4;
    dev->calib.res_heat_val = (int8_t)res_heat_val;
    dev->calib.range_sw_err = ((int8_t)range_sw_err & 0xF0) >> 4;

    return BME680_OK;
}

// Compensation formulas (from BME680 datasheet)

static float bme680_compensate_temperature(bme680_dev_t *dev, uint32_t temp_adc) {
    int64_t var1, var2;

    var1 = ((int32_t)temp_adc >> 3) - ((int32_t)dev->calib.par_t1 << 1);
    var2 = (var1 * (int32_t)dev->calib.par_t2) >> 11;
    var1 = ((var1 >> 1) * (var1 >> 1)) >> 12;
    var1 = ((var1) * ((int32_t)dev->calib.par_t3 << 4)) >> 14;
    dev->calib.t_fine = (int32_t)(var2 + var1);

    return ((dev->calib.t_fine * 5) + 128) / 256.0f / 100.0f;
}

static float bme680_compensate_humidity(bme680_dev_t *dev, uint16_t hum_adc) {
    int32_t var1, var2, var3, var4, var5, var6, temp_scaled, calc_hum;

    temp_scaled = (((int32_t)dev->calib.t_fine * 5) + 128) >> 8;
    var1 = (int32_t)hum_adc - ((int32_t)((int32_t)dev->calib.par_h1 << 4)) -
           (((temp_scaled * (int32_t)dev->calib.par_h3) / 100) >> 1);
    var2 = ((int32_t)dev->calib.par_h2 *
           (((temp_scaled * (int32_t)dev->calib.par_h4) / 100) +
            (((temp_scaled * ((temp_scaled * (int32_t)dev->calib.par_h5) / 100)) >> 6) / 100) +
            (1 << 14))) >> 10;
    var3 = var1 * var2;
    var4 = (int32_t)dev->calib.par_h6 << 7;
    var4 = ((var4) + ((temp_scaled * (int32_t)dev->calib.par_h7) / 100)) >> 4;
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
    var6 = (var4 * var5) >> 1;
    calc_hum = (((var3 + var6) >> 10) * 1000) >> 12;

    if (calc_hum > 100000) calc_hum = 100000;
    else if (calc_hum < 0) calc_hum = 0;

    return calc_hum / 1000.0f;
}

static float bme680_compensate_pressure(bme680_dev_t *dev, uint32_t press_adc) {
    int32_t var1, var2, var3, calc_press;

    var1 = (((int32_t)dev->calib.t_fine) >> 1) - 64000;
    var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)dev->calib.par_p6) >> 2;
    var2 = var2 + ((var1 * (int32_t)dev->calib.par_p5) << 1);
    var2 = (var2 >> 2) + ((int32_t)dev->calib.par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) *
            ((int32_t)dev->calib.par_p3 << 5)) >> 3) +
           (((int32_t)dev->calib.par_p2 * var1) >> 1);
    var1 = var1 >> 18;
    var1 = ((32768 + var1) * (int32_t)dev->calib.par_p1) >> 15;
    calc_press = 1048576 - press_adc;
    calc_press = (int32_t)((calc_press - (var2 >> 12)) * (3125));

    if (calc_press >= (1 << 30))
        calc_press = ((calc_press / (uint32_t)var1) << 1);
    else
        calc_press = ((calc_press << 1) / (uint32_t)var1);

    var1 = ((int32_t)dev->calib.par_p9 * (int32_t)(((calc_press >> 3) * (calc_press >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(calc_press >> 2) * (int32_t)dev->calib.par_p8) >> 13;
    var3 = ((int32_t)(calc_press >> 8) * (int32_t)(calc_press >> 8) *
           (int32_t)(calc_press >> 8) * (int32_t)dev->calib.par_p10) >> 17;

    calc_press = (int32_t)(calc_press) + ((var1 + var2 + var3 + ((int32_t)dev->calib.par_p7 << 7)) >> 4);

    return calc_press / 100.0f;
}

static uint32_t bme680_compensate_gas(bme680_dev_t *dev, uint16_t gas_adc, uint8_t gas_range) {
    int64_t var1;
    uint64_t var2;
    int64_t var3;
    uint32_t calc_gas_res;

    const uint32_t lookupTable1[16] = {
        2147483647u, 2147483647u, 2147483647u, 2147483647u,
        2147483647u, 2126008810u, 2147483647u, 2130303777u,
        2147483647u, 2147483647u, 2143188679u, 2136746228u,
        2147483647u, 2126008810u, 2147483647u, 2147483647u
    };

    const uint32_t lookupTable2[16] = {
        4096000000u, 2048000000u, 1024000000u, 512000000u,
        255744255u, 127110228u, 64000000u, 32258064u,
        16016016u, 8000000u, 4000000u, 2000000u,
        1000000u, 500000u, 250000u, 125000u
    };

    var1 = (int64_t)((1340 + (5 * (int64_t)dev->calib.range_sw_err)) *
           ((int64_t)lookupTable1[gas_range])) >> 16;
    var2 = (((int64_t)((int64_t)gas_adc << 15) - (int64_t)(16777216)) + var1);
    var3 = (((int64_t)lookupTable2[gas_range] * (int64_t)var1) >> 9);
    calc_gas_res = (uint32_t)((var3 + ((int64_t)var2 >> 1)) / (int64_t)var2);

    return calc_gas_res;
}

// Calculate the RES_HEAT_x register value for a target heater temperature.
//
// This is Bosch's reference integer formula. The previous implementation was
// broken in two ways that left the heater cold (gas plate never reached target,
// so gas resistance saturated at a constant): it truncated `par_g2 / 32768` to
// zero via integer division, and it ignored the heater-calibration coefficients
// `res_heat_range` / `res_heat_val` entirely. Both are required here.
static uint8_t bme680_calc_heater_res(bme680_dev_t *dev, uint16_t temp) {
    int32_t var1, var2, var3, var4, var5;
    int32_t heatr_res_x100;

    if (temp > 400) temp = 400;  // cap at datasheet maximum

    var1 = (((int32_t)dev->amb_temp * dev->calib.par_g3) / 1000) * 256;
    var2 = ((int32_t)dev->calib.par_g1 + 784) *
           (((((int32_t)dev->calib.par_g2 + 154009) * (int32_t)temp * 5) / 100) + 3276800) / 10;
    var3 = var1 + (var2 / 2);
    var4 = var3 / ((int32_t)dev->calib.res_heat_range + 4);
    var5 = (131 * (int32_t)dev->calib.res_heat_val) + 65536;
    heatr_res_x100 = ((var4 / var5) - 250) * 34;

    return (uint8_t)((heatr_res_x100 + 50) / 100);
}

// Encode a heater duration (milliseconds) into the GAS_WAIT_x register format:
// a 6-bit step count in bits[5:0] with a x1/x4/x16/x64 multiplier in bits[7:6].
// The previous code wrote `dur/4` as if the field were in 4 ms units, which
// under-drove the heater (e.g. 150 ms -> 37 ms of actual heating).
static uint8_t bme680_calc_heater_dur(uint16_t dur_ms) {
    uint8_t factor = 0;
    uint8_t durval;

    if (dur_ms >= 0xFC0) {
        durval = 0xFF;  // clamp at register maximum (~4032 ms)
    } else {
        while (dur_ms > 0x3F) {
            dur_ms /= 4;
            factor += 1;
        }
        durval = (uint8_t)(dur_ms + (factor * 64));
    }
    return durval;
}

// Public API functions

bme680_error_t bme680_init(bme680_dev_t *dev, i2c_inst_t *i2c_port, uint8_t i2c_addr) {
    dev->i2c_port = i2c_port;
    dev->i2c_addr = i2c_addr;

    // Set default configuration
    dev->temp_os = BME680_OSR_2X;
    dev->hum_os = BME680_OSR_2X;
    dev->press_os = BME680_OSR_2X;
    dev->filter = BME680_FILTER_3;
    dev->heater_temp = 320;  // 320°C for VOC detection
    dev->heater_dur = 150;   // 150ms
    dev->amb_temp = 25;      // ambient-temp estimate for heater resistance calc

    // Soft reset
    bme680_error_t err = bme680_soft_reset(dev);
    if (err != BME680_OK) return err;

    sleep_ms(10);  // Wait for reset

    // Verify chip ID
    uint8_t chip_id;
    err = bme680_get_chip_id(dev, &chip_id);
    if (err != BME680_OK) return err;
    if (chip_id != BME680_CHIP_ID) return BME680_ERR_CHIP_ID;

    // Load calibration coefficients
    err = bme680_load_calibration(dev);
    if (err != BME680_OK) return err;

    // Configure sensor
    return bme680_configure(dev);
}

bme680_error_t bme680_soft_reset(bme680_dev_t *dev) {
    if (bme680_write_reg(dev, BME680_REG_RESET, BME680_SOFT_RESET_CMD) < 0) {
        return BME680_ERR_I2C;
    }
    return BME680_OK;
}

bme680_error_t bme680_get_chip_id(bme680_dev_t *dev, uint8_t *chip_id) {
    if (bme680_read_reg(dev, BME680_REG_CHIP_ID, chip_id) < 0) {
        return BME680_ERR_I2C;
    }
    return BME680_OK;
}

bme680_error_t bme680_configure(bme680_dev_t *dev) {
    // Set humidity oversampling
    uint8_t hum_ctrl = dev->hum_os & 0x07;
    if (bme680_write_reg(dev, BME680_REG_CTRL_HUM, hum_ctrl) < 0) {
        return BME680_ERR_I2C;
    }

    // Set temperature and pressure oversampling, mode = sleep
    uint8_t meas_ctrl = (dev->temp_os << 5) | (dev->press_os << 2) | BME680_MODE_SLEEP;
    if (bme680_write_reg(dev, BME680_REG_CTRL_MEAS, meas_ctrl) < 0) {
        return BME680_ERR_I2C;
    }

    // Set IIR filter
    uint8_t config = (dev->filter << 2);
    if (bme680_write_reg(dev, BME680_REG_CONFIG, config) < 0) {
        return BME680_ERR_I2C;
    }

    // Configure gas sensor heater
    uint8_t heater_res = bme680_calc_heater_res(dev, dev->heater_temp);
    if (bme680_write_reg(dev, BME680_REG_RES_HEAT_0, heater_res) < 0) {
        return BME680_ERR_I2C;
    }

    // Set heater duration (encoded step-count + multiplier)
    uint8_t gas_wait = bme680_calc_heater_dur(dev->heater_dur);
    if (bme680_write_reg(dev, BME680_REG_GAS_WAIT_0, gas_wait) < 0) {
        return BME680_ERR_I2C;
    }

    // Enable gas measurements (nb_conv=0 for heater setpoint 0)
    uint8_t gas_ctrl = 0x10;  // run_gas = 1, nb_conv = 0
    if (bme680_write_reg(dev, BME680_REG_CTRL_GAS_1, gas_ctrl) < 0) {
        return BME680_ERR_I2C;
    }

    return BME680_OK;
}

bme680_error_t bme680_trigger_measurement(bme680_dev_t *dev) {
    // Read current ctrl_meas value
    uint8_t ctrl_meas;
    if (bme680_read_reg(dev, BME680_REG_CTRL_MEAS, &ctrl_meas) < 0) {
        return BME680_ERR_I2C;
    }

    // Set mode to forced (bits 1:0 = 01)
    ctrl_meas = (ctrl_meas & 0xFC) | BME680_MODE_FORCED;
    if (bme680_write_reg(dev, BME680_REG_CTRL_MEAS, ctrl_meas) < 0) {
        return BME680_ERR_I2C;
    }

    return BME680_OK;
}

bme680_error_t bme680_is_measuring(bme680_dev_t *dev, bool *measuring) {
    uint8_t status;
    if (bme680_read_reg(dev, BME680_REG_MEAS_STATUS_0, &status) < 0) {
        return BME680_ERR_I2C;
    }

    *measuring = (status & 0x20) != 0;  // Bit 5 = measuring
    return BME680_OK;
}

bme680_error_t bme680_read_data(bme680_dev_t *dev, bme680_data_t *data) {
    uint8_t buf[15];  // All measurement data

    // Read all measurement registers (0x1F to 0x2D)
    if (bme680_read_regs(dev, BME680_REG_PRESS_MSB, buf, 15) < 0) {
        return BME680_ERR_I2C;
    }

    // Parse raw ADC values
    uint32_t press_adc = (uint32_t)((buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4));
    uint32_t temp_adc = (uint32_t)((buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4));
    uint16_t hum_adc = (uint16_t)((buf[6] << 8) | buf[7]);
    uint16_t gas_adc = (uint16_t)((buf[11] << 2) | (buf[12] >> 6));
    uint8_t gas_range = buf[12] & 0x0F;

    // Check gas measurement validity: require BOTH gas_valid_r (bit 5) and
    // heat_stab_r (bit 4). The old `!= 0` accepted a reading even when the
    // heater never stabilised, masking a cold-plate / saturated-gas fault.
    data->gas_valid = (buf[12] & 0x30) == 0x30;

    // Compensate measurements
    data->temperature = bme680_compensate_temperature(dev, temp_adc);
    data->humidity = bme680_compensate_humidity(dev, hum_adc);
    data->pressure = bme680_compensate_pressure(dev, press_adc);
    data->gas_resistance = bme680_compensate_gas(dev, gas_adc, gas_range);

    return BME680_OK;
}

const char* bme680_error_string(bme680_error_t err) {
    switch (err) {
        case BME680_OK: return "OK";
        case BME680_ERR_I2C: return "I2C_ERROR";
        case BME680_ERR_CHIP_ID: return "INVALID_CHIP_ID";
        case BME680_ERR_INVALID_DATA: return "INVALID_DATA";
        case BME680_ERR_NOT_READY: return "NOT_READY";
        case BME680_ERR_GAS_HEATER: return "GAS_HEATER_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}
