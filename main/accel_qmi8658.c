#include "accel_qmi8658.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

#define I2C_PORT_NUM            I2C_NUM_0
#define I2C_DEFAULT_SDA_GPIO    18
#define I2C_DEFAULT_SCL_GPIO    8
#define I2C_FREQ_HZ             400000

#define QMI8658_ADDR_PRIMARY    0x6B
#define QMI8658_ADDR_SECONDARY  0x6A

#define REG_WHO_AM_I            0x00
#define REG_CTRL1               0x02
#define REG_CTRL2               0x03
#define REG_CTRL7               0x08
#define REG_CTRL8               0x09
#define REG_AX_L                0x35

static const char *TAG = "ACCEL";

static uint8_t s_addr = 0;
static bool s_ready = false;
static bool s_i2c_inited = false;

static esp_err_t accel_i2c_init_once(void)
{
    if (s_i2c_inited) {
        return ESP_OK;
    }

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_DEFAULT_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_DEFAULT_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(I2C_PORT_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config fallo: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_PORT_NUM, cfg.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE || err == ESP_FAIL) {
        ESP_LOGW(TAG, "i2c_driver_install retorno %s; reutilizando bus existente", esp_err_to_name(err));
        s_i2c_inited = true;
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install fallo: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C accel inicializado en SDA=%d SCL=%d", I2C_DEFAULT_SDA_GPIO, I2C_DEFAULT_SCL_GPIO);
    s_i2c_inited = true;
    return ESP_OK;
}

static esp_err_t accel_rd(uint8_t addr, uint8_t reg, void *buf, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT_NUM, addr, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

static esp_err_t accel_wr(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT_NUM, addr, data, sizeof(data), pdMS_TO_TICKS(100));
}

static bool accel_probe_addr(uint8_t addr, uint8_t *who_am_i)
{
    uint8_t who = 0;
    if (accel_rd(addr, REG_WHO_AM_I, &who, 1) == ESP_OK) {
        if (who_am_i) {
            *who_am_i = who;
        }
        return true;
    }
    return false;
}

esp_err_t accel_qmi8658_init(void)
{
    s_ready = false;
    s_addr = 0;

    esp_err_t err = accel_i2c_init_once();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t who = 0;
    if (accel_probe_addr(QMI8658_ADDR_PRIMARY, &who)) {
        s_addr = QMI8658_ADDR_PRIMARY;
    } else if (accel_probe_addr(QMI8658_ADDR_SECONDARY, &who)) {
        s_addr = QMI8658_ADDR_SECONDARY;
    } else {
        ESP_LOGE(TAG, "No encuentro QMI8658 en 0x6B/0x6A (SDA=%d SCL=%d)",
                 I2C_DEFAULT_SDA_GPIO, I2C_DEFAULT_SCL_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "QMI8658 detectado addr=0x%02X WHO_AM_I=0x%02X", s_addr, who);

    err = accel_wr(s_addr, REG_CTRL1, (1 << 6)); // auto increment
    if (err != ESP_OK) return err;

    err = accel_wr(s_addr, REG_CTRL8, (1 << 7)); // data processing enable
    if (err != ESP_OK) return err;

    err = accel_wr(s_addr, REG_CTRL7, 0x00); // disable all
    if (err != ESP_OK) return err;

    err = accel_wr(s_addr, REG_CTRL2, 0x06); // ACC +/-2g (16384 LSB/g), ODR default profile
    if (err != ESP_OK) return err;

    err = accel_wr(s_addr, REG_CTRL7, 0x01); // accel only enable
    if (err != ESP_OK) return err;

    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 listo (accel-only)");
    return ESP_OK;
}

esp_err_t accel_qmi8658_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    if (!x || !y || !z) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready || s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6] = {0};
    esp_err_t err = accel_rd(s_addr, REG_AX_L, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
    return ESP_OK;
}

bool accel_qmi8658_is_ready(void)
{
    return s_ready;
}

void accel_qmi8658_get_i2c_pins(int *sda, int *scl)
{
    if (sda) *sda = I2C_DEFAULT_SDA_GPIO;
    if (scl) *scl = I2C_DEFAULT_SCL_GPIO;
}
