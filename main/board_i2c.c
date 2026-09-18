#include "board_i2c.h"

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "BOARD_I2C";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_devices[128];
static StaticSemaphore_t s_device_lock_storage;
static SemaphoreHandle_t s_device_lock;

esp_err_t board_i2c_init(void)
{
    if (s_bus) return ESP_OK;

    const i2c_master_bus_config_t config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t result = i2c_new_master_bus(&config, &s_bus);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar I2C: %s", esp_err_to_name(result));
        return result;
    }

    s_device_lock = xSemaphoreCreateMutexStatic(&s_device_lock_storage);
    if (!s_device_lock) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bus compartido listo SDA=%d SCL=%d @ %d Hz",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

static esp_err_t board_i2c_get_device(uint8_t address, i2c_master_dev_handle_t *device)
{
    if (!device || address >= 0x80U) return ESP_ERR_INVALID_ARG;

    esp_err_t result = board_i2c_init();
    if (result != ESP_OK) return result;

    if (xSemaphoreTake(s_device_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_devices[address]) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = BOARD_I2C_FREQ_HZ,
        };
        result = i2c_master_bus_add_device(s_bus, &config, &s_devices[address]);
    }

    *device = s_devices[address];
    xSemaphoreGive(s_device_lock);
    return result;
}

esp_err_t board_i2c_probe(uint8_t address, int timeout_ms)
{
    if (address >= 0x80U) return ESP_ERR_INVALID_ARG;
    esp_err_t result = board_i2c_init();
    if (result != ESP_OK) return result;
    return i2c_master_probe(s_bus, address, timeout_ms);
}

esp_err_t board_i2c_read_reg(uint8_t address, uint8_t reg, void *data, size_t len, int timeout_ms)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    i2c_master_dev_handle_t device = NULL;
    esp_err_t result = board_i2c_get_device(address, &device);
    if (result != ESP_OK) return result;
    return i2c_master_transmit_receive(device, &reg, 1, data, len, timeout_ms);
}

esp_err_t board_i2c_write_reg(uint8_t address, uint8_t reg, uint8_t value, int timeout_ms)
{
    i2c_master_dev_handle_t device = NULL;
    esp_err_t result = board_i2c_get_device(address, &device);
    if (result != ESP_OK) return result;
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(device, payload, sizeof(payload), timeout_ms);
}
