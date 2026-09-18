#include "board_i2c.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BOARD_I2C";
static bool s_ready;

esp_err_t board_i2c_init(void)
{
    if (s_ready) return ESP_OK;

    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };

    esp_err_t result = i2c_param_config(BOARD_I2C_PORT, &config);
    if (result != ESP_OK) return result;

    result = i2c_driver_install(BOARD_I2C_PORT, config.mode, 0, 0, 0);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

    s_ready = true;
    ESP_LOGI(TAG, "Bus compartido listo SDA=%d SCL=%d @ %d Hz",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

esp_err_t board_i2c_read_reg(uint8_t address, uint8_t reg, void *data, size_t len, TickType_t timeout)
{
    if (!s_ready) {
        esp_err_t result = board_i2c_init();
        if (result != ESP_OK) return result;
    }
    return i2c_master_write_read_device(BOARD_I2C_PORT, address, &reg, 1, data, len, timeout);
}

esp_err_t board_i2c_write_reg(uint8_t address, uint8_t reg, uint8_t value, TickType_t timeout)
{
    if (!s_ready) {
        esp_err_t result = board_i2c_init();
        if (result != ESP_OK) return result;
    }
    uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(BOARD_I2C_PORT, address, payload, sizeof(payload), timeout);
}
