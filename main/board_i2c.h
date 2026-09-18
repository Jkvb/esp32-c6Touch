#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SCL_GPIO 8
#define BOARD_I2C_SDA_GPIO 18
#define BOARD_I2C_FREQ_HZ 400000

esp_err_t board_i2c_init(void);
esp_err_t board_i2c_probe(uint8_t address, int timeout_ms);
esp_err_t board_i2c_read_reg(uint8_t address, uint8_t reg, void *data, size_t len, int timeout_ms);
esp_err_t board_i2c_write_reg(uint8_t address, uint8_t reg, uint8_t value, int timeout_ms);
