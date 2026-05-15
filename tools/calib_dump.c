#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define I2C_PORT     i2c0
#define I2C_SDA_PIN  4
#define I2C_SCL_PIN  5
#define I2C_FREQ_HZ  100000
#define BME680_ADDR  0x77

static int read_regs(uint8_t reg, uint8_t *buf, uint8_t len) {
    int r = i2c_write_blocking(I2C_PORT, BME680_ADDR, &reg, 1, true);
    if (r < 0) return r;
    return i2c_read_blocking(I2C_PORT, BME680_ADDR, buf, len, false);
}

static int read_reg(uint8_t reg, uint8_t *val) {
    return read_regs(reg, val, 1);
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    i2c_init(I2C_PORT, I2C_FREQ_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    sleep_ms(100);

    while (true) {
        printf("\n[CALIB] === begin dump ===\n");

        uint8_t chip_id = 0xFF;
        int r = read_reg(0xD0, &chip_id);
        printf("[CALIB] chip_id=0x%02X (expect 0x61) i2c_ret=%d\n", chip_id, r);

        uint8_t block1[25] = {0};
        r = read_regs(0x89, block1, 25);
        printf("[CALIB] block1_start=0x89 len=25 i2c_ret=%d\n", r);
        for (int i = 0; i < 25; i++) {
            printf("[CALIB] reg=0x%02X buf1[%2d]=0x%02X\n", 0x89 + i, i, block1[i]);
        }

        uint8_t block2[16] = {0};
        r = read_regs(0xE1, block2, 16);
        printf("[CALIB] block2_start=0xE1 len=16 i2c_ret=%d\n", r);
        for (int i = 0; i < 16; i++) {
            printf("[CALIB] reg=0x%02X buf2[%2d]=0x%02X\n", 0xE1 + i, i, block2[i]);
        }

        uint8_t res_heat_val = 0, res_heat_range = 0, range_sw_err = 0;
        read_reg(0x00, &res_heat_val);
        read_reg(0x02, &res_heat_range);
        read_reg(0x04, &range_sw_err);
        printf("[CALIB] heat: reg_0x00=0x%02X reg_0x02=0x%02X reg_0x04=0x%02X\n",
               res_heat_val, res_heat_range, range_sw_err);

        printf("[CALIB] === parsed (Bosch-correct indexing) ===\n");
        uint16_t par_t1 = ((uint16_t)block2[9] << 8) | block2[8];
        int16_t  par_t2 = ((int16_t) block1[2] << 8) | block1[1];
        int8_t   par_t3 = (int8_t)block1[3];
        uint16_t par_p1 = ((uint16_t)block1[6] << 8) | block1[5];
        int16_t  par_p2 = ((int16_t) block1[8] << 8) | block1[7];
        uint16_t par_h1 = ((uint16_t)block2[2] << 4) | (block2[1] & 0x0F);
        uint16_t par_h2 = ((uint16_t)block2[0] << 4) | (block2[1] >> 4);
        printf("[CALIB] par_t1=%u par_t2=%d par_t3=%d\n", par_t1, par_t2, par_t3);
        printf("[CALIB] par_p1=%u par_p2=%d\n", par_p1, par_p2);
        printf("[CALIB] par_h1=%u par_h2=%u\n", par_h1, par_h2);

        printf("[CALIB] === parsed (current driver, off-by-one) ===\n");
        uint16_t bad_par_t1 = ((uint16_t)block2[8] << 8) | block2[7];
        int16_t  bad_par_t2 = ((int16_t) block1[1] << 8) | block1[0];
        int8_t   bad_par_t3 = (int8_t)block1[2];
        uint16_t bad_par_h1 = ((uint16_t)block2[1] << 4) | (block2[0] & 0x0F);
        printf("[CALIB] par_t1=%u par_t2=%d par_t3=%d\n", bad_par_t1, bad_par_t2, bad_par_t3);
        printf("[CALIB] par_h1=%u\n", bad_par_h1);

        printf("[CALIB] === end dump ===\n");
        sleep_ms(5000);
    }
}
