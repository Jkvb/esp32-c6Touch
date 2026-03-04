#include "imu_qmi8658.h"
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "IMU";

#define I2C_PORT        I2C_NUM_0
#define PIN_SCL         8
#define PIN_SDA         18
#define I2C_FREQ_HZ     400000

#define QMI_ADDR_1      0x6B
#define QMI_ADDR_2      0x6A

#define REG_WHOAMI      0x00
#define REG_CTRL1       0x02
#define REG_CTRL2       0x03
#define REG_CTRL7       0x08
#define REG_CTRL8       0x09
#define REG_AX_L        0x35

static uint8_t s_addr = 0;

static esp_err_t i2c_init_once(void)
{
    static bool inited = false;
    if (inited) return ESP_OK;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));
    inited = true;
    return ESP_OK;
}

static esp_err_t rd(uint8_t addr, uint8_t reg, void *buf, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

static esp_err_t wr(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t d[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, addr, d, sizeof(d), pdMS_TO_TICKS(100));
}

static bool probe_addr(uint8_t addr, uint8_t *who)
{
    uint8_t v = 0;
    if (rd(addr, REG_WHOAMI, &v, 1) == ESP_OK) {
        if (who) *who = v;
        return true;
    }
    return false;
}

esp_err_t imu_qmi8658_init(void)
{
    ESP_ERROR_CHECK(i2c_init_once());

    uint8_t who = 0;
    if (probe_addr(QMI_ADDR_1, &who)) s_addr = QMI_ADDR_1;
    else if (probe_addr(QMI_ADDR_2, &who)) s_addr = QMI_ADDR_2;
    else {
        ESP_LOGE(TAG, "No encuentro QMI8658 en 0x6B/0x6A. (I2C pins?)");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QMI8658 detectado addr=0x%02X WHOAMI=0x%02X", s_addr, who);

    ESP_ERROR_CHECK(wr(s_addr, REG_CTRL1, (1 << 6)));
    ESP_ERROR_CHECK(wr(s_addr, REG_CTRL8, (1 << 7)));
    ESP_ERROR_CHECK(wr(s_addr, REG_CTRL7, 0x00));
    ESP_ERROR_CHECK(wr(s_addr, REG_CTRL2, 0x06));
    ESP_ERROR_CHECK(wr(s_addr, REG_CTRL7, 0x01));
    return ESP_OK;
}

esp_err_t imu_qmi8658_read_accel(imu_accel_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_addr) return ESP_ERR_INVALID_STATE;

    uint8_t b[6];
    ESP_ERROR_CHECK(rd(s_addr, REG_AX_L, b, sizeof(b)));

    int16_t rx = (int16_t)((b[1] << 8) | b[0]);
    int16_t ry = (int16_t)((b[3] << 8) | b[2]);
    int16_t rz = (int16_t)((b[5] << 8) | b[4]);

    const float scale = 1.0f / 16384.0f;
    out->ax = rx * scale;
    out->ay = ry * scale;
    out->az = rz * scale;
    return ESP_OK;
}