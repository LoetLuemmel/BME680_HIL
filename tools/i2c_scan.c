#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define I2C_PORT     i2c0
#define I2C_SDA_PIN  4
#define I2C_SCL_PIN  5
#define I2C_FREQ_HZ  100000

static int probe_addr(uint8_t addr) {
    uint8_t dummy = 0;
    int r = i2c_read_blocking(I2C_PORT, addr, &dummy, 1, false);
    return r >= 0;
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

    uint32_t sweep = 0;
    while (true) {
        printf("\n[SCAN] sweep=%lu  SDA=GP%d SCL=GP%d  %d Hz\n",
               sweep, I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
        printf("       ");
        for (int col = 0; col < 16; col++) printf(" %x ", col);
        printf("\n");

        int found = 0;
        for (int row = 0; row < 8; row++) {
            printf("  %x0:  ", row);
            for (int col = 0; col < 16; col++) {
                uint8_t addr = (row << 4) | col;
                if (addr < 0x08 || addr > 0x77) {
                    printf(" . ");
                    continue;
                }
                if (probe_addr(addr)) {
                    printf("%02X ", addr);
                    found++;
                } else {
                    printf("-- ");
                }
            }
            printf("\n");
        }

        printf("[SCAN] total_found=%d expected_bme680=0x76_or_0x77\n", found);
        sweep++;
        sleep_ms(2000);
    }
}
