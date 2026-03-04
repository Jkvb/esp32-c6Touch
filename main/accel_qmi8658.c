#include "accel_qmi8658.h"

#include "driver/i2c.h"
#include "esp_log.h"

#define I2C_PORT_NUM            I2C_NUM_0
#define I2C_SDA_GPIO            20
#define I2C_SCL_GPIO            21
#define I2C_FREQ_HZ             400000

#define QMI8658_ADDR1           0x6A
#define QMI8658_ADDR2           0x6B

#define REG_WHO_AM_I            0x00
#define REG_CTRL1               0x02
#define REG_CTRL2               0x03
#define REG_CTRL7               0x08
#define REG_AX_L                0x35

static const char *TAG = "ACCEL";
static uint8_t s_addr = 0;
static bool s_ready = false;

static esp_err_t accel_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT_NUM, s_addr, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t accel_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(I2C_PORT_NUM, s_addr, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

esp_err_t accel_qmi8658_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(I2C_PORT_NUM, &cfg);
    if (err != ESP_OK) return err;

    err = i2c_driver_install(I2C_PORT_NUM, cfg.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    uint8_t who = 0;
    const uint8_t addrs[] = {QMI8658_ADDR1, QMI8658_ADDR2};
    for (size_t i = 0; i < (sizeof(addrs) / sizeof(addrs[0])); i++) {
        s_addr = addrs[i];
        if (accel_read_reg(REG_WHO_AM_I, &who) == ESP_OK) {
            ESP_LOGI(TAG, "QMI8658 detectado en 0x%02X (WHO_AM_I=0x%02X)", s_addr, who);
            s_ready = true;
            break;
        }
    }

    if (!s_ready) {
        ESP_LOGW(TAG, "No se detecto QMI8658 en I2C (SDA=%d, SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    err = accel_write_reg(REG_CTRL1, 0x40); // auto increment
    if (err != ESP_OK) return err;

    err = accel_write_reg(REG_CTRL2, 0x15); // ACC +/-4g, ODR 128Hz
    if (err != ESP_OK) return err;

    err = accel_write_reg(REG_CTRL7, 0x01); // enable accel only
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t accel_qmi8658_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg = REG_AX_L;
    uint8_t data[6] = {0};
    esp_err_t err = i2c_master_write_read_device(I2C_PORT_NUM, s_addr, &reg, 1, data, sizeof(data), pdMS_TO_TICKS(100));
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
