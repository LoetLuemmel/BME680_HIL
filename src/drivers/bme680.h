/**
 * @file bme680.h
 * @brief BME680 environmental sensor driver for Raspberry Pi Pico
 *
 * Driver for Bosch BME680 4-in-1 environmental sensor:
 * - Temperature: -40 to +85°C
 * - Humidity: 0-100% RH
 * - Pressure: 300-1100 hPa
 * - Gas resistance: 10kΩ - 2MΩ (requires heater)
 *
 * This is iteration 1: naive driver with minimal error handling.
 */

#ifndef BME680_H
#define BME680_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

// I2C addresses (SDO pin determines address)
#define BME680_I2C_ADDR_PRIMARY     0x77  // SDO high or floating
#define BME680_I2C_ADDR_SECONDARY   0x76  // SDO low (GND)

// Register addresses
#define BME680_REG_CHIP_ID          0xD0  // Should read 0x61
#define BME680_REG_RESET            0xE0  // Write 0xB6 for soft reset
#define BME680_REG_CTRL_GAS_1       0x71  // Gas sensor control
#define BME680_REG_CTRL_HUM         0x72  // Humidity oversampling
#define BME680_REG_STATUS           0x73  // Status flags
#define BME680_REG_CTRL_MEAS        0x74  // Pressure/temp oversampling + mode
#define BME680_REG_CONFIG           0x75  // IIR filter config
#define BME680_REG_MEAS_STATUS_0    0x1D  // Measurement status
#define BME680_REG_PRESS_MSB        0x1F  // Pressure data start (3 bytes)
#define BME680_REG_TEMP_MSB         0x22  // Temperature data start (3 bytes)
#define BME680_REG_HUM_MSB          0x25  // Humidity data start (2 bytes)
#define BME680_REG_GAS_R_MSB        0x2A  // Gas resistance MSB
#define BME680_REG_GAS_R_LSB        0x2B  // Gas resistance LSB (also contains range)

// Gas heater registers
#define BME680_REG_RES_HEAT_0       0x5A  // Heater resistance (for temp control)
#define BME680_REG_GAS_WAIT_0       0x64  // Heater duration
#define BME680_REG_CTRL_GAS_0       0x70  // Gas sensor control (run_gas, nb_conv)

// Calibration coefficient registers
#define BME680_REG_COEFF_1_START    0x89  // Calibration block 1 (25 bytes)
#define BME680_REG_COEFF_2_START    0xE1  // Calibration block 2 (16 bytes)

// Constants
#define BME680_CHIP_ID              0x61
#define BME680_SOFT_RESET_CMD       0xB6

// Operating modes
typedef enum {
    BME680_MODE_SLEEP   = 0x00,
    BME680_MODE_FORCED  = 0x01,
} bme680_mode_t;

// Oversampling settings
typedef enum {
    BME680_OSR_NONE = 0,
    BME680_OSR_1X   = 1,
    BME680_OSR_2X   = 2,
    BME680_OSR_4X   = 3,
    BME680_OSR_8X   = 4,
    BME680_OSR_16X  = 5,
} bme680_osr_t;

// IIR filter coefficients
typedef enum {
    BME680_FILTER_OFF = 0,
    BME680_FILTER_1   = 1,
    BME680_FILTER_3   = 2,
    BME680_FILTER_7   = 3,
    BME680_FILTER_15  = 4,
    BME680_FILTER_31  = 5,
    BME680_FILTER_63  = 6,
    BME680_FILTER_127 = 7,
} bme680_filter_t;

// Error codes
typedef enum {
    BME680_OK               = 0,
    BME680_ERR_I2C          = -1,
    BME680_ERR_CHIP_ID      = -2,
    BME680_ERR_INVALID_DATA = -3,
    BME680_ERR_NOT_READY    = -4,
    BME680_ERR_GAS_HEATER   = -5,
} bme680_error_t;

// Calibration coefficients (loaded from sensor)
typedef struct {
    // Temperature calibration
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;

    // Pressure calibration
    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int16_t  par_p4;
    int16_t  par_p5;
    int8_t   par_p6;
    int8_t   par_p7;
    int16_t  par_p8;
    int16_t  par_p9;
    uint8_t  par_p10;

    // Humidity calibration
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int8_t   par_h5;
    uint8_t  par_h6;
    int8_t   par_h7;

    // Gas sensor calibration
    int8_t   par_g1;
    int16_t  par_g2;
    int8_t   par_g3;

    // Heater calibration
    uint8_t  res_heat_range;
    int8_t   res_heat_val;
    int8_t   range_sw_err;

    // Fine temperature value (used in compensation)
    int32_t  t_fine;
} bme680_calib_t;

// Sensor configuration
typedef struct {
    i2c_inst_t *i2c_port;
    uint8_t i2c_addr;
    bme680_osr_t temp_os;
    bme680_osr_t hum_os;
    bme680_osr_t press_os;
    bme680_filter_t filter;
    uint16_t heater_temp;  // Target heater temp in °C (200-400)
    uint16_t heater_dur;   // Heater duration in ms (1-4032)
    bme680_calib_t calib;
} bme680_dev_t;

// Measurement data
typedef struct {
    float temperature;      // °C
    float humidity;         // % RH
    float pressure;         // hPa
    uint32_t gas_resistance; // Ohms
    bool gas_valid;         // Gas measurement valid flag
} bme680_data_t;

// Function prototypes

/**
 * @brief Initialize BME680 sensor
 * @param dev Pointer to device structure
 * @param i2c_port I2C peripheral instance (i2c0 or i2c1)
 * @param i2c_addr I2C address (0x76 or 0x77)
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_init(bme680_dev_t *dev, i2c_inst_t *i2c_port, uint8_t i2c_addr);

/**
 * @brief Soft reset the sensor
 * @param dev Pointer to device structure
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_soft_reset(bme680_dev_t *dev);

/**
 * @brief Read chip ID
 * @param dev Pointer to device structure
 * @param chip_id Pointer to store chip ID
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_get_chip_id(bme680_dev_t *dev, uint8_t *chip_id);

/**
 * @brief Configure sensor settings
 * @param dev Pointer to device structure
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_configure(bme680_dev_t *dev);

/**
 * @brief Trigger a forced mode measurement
 * @param dev Pointer to device structure
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_trigger_measurement(bme680_dev_t *dev);

/**
 * @brief Check if measurement is ready
 * @param dev Pointer to device structure
 * @param ready Pointer to store ready flag
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_is_measuring(bme680_dev_t *dev, bool *measuring);

/**
 * @brief Read measurement data
 * @param dev Pointer to device structure
 * @param data Pointer to store measurement data
 * @return BME680_OK on success, error code otherwise
 */
bme680_error_t bme680_read_data(bme680_dev_t *dev, bme680_data_t *data);

/**
 * @brief Get error string for error code
 * @param err Error code
 * @return Error string
 */
const char* bme680_error_string(bme680_error_t err);

#endif // BME680_H
