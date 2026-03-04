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

static esp_err_t i2c_ping(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(40));
    i2c_cmd_link_delete(cmd);
    return err;
}

static void i2c_scan_log(void)
{
    char found[192] = {0};
    size_t used = 0;
    int count = 0;
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        if (i2c_ping(addr) == ESP_OK) {
            int n = snprintf(found + used, sizeof(found) - used, "0x%02X ", addr);
            if (n > 0 && (size_t)n < (sizeof(found) - used)) used += (size_t)n;
            count++;
        }
    }
    if (count > 0) {
        ESP_LOGI(TAG, "I2C scan SDA=%d SCL=%d -> %d dev(s): %s", I2C_SDA_GPIO, I2C_SCL_GPIO, count, found);
    } else {
        ESP_LOGW(TAG, "I2C scan SDA=%d SCL=%d -> sin dispositivos", I2C_SDA_GPIO, I2C_SCL_GPIO);
    }
}

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

    esp_err_t cfg_err = i2c_param_config(I2C_PORT_NUM, &cfg);
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "i2c_param_config accel retorno %s", esp_err_to_name(cfg_err));
    }

    esp_err_t install_err = i2c_driver_install(I2C_PORT_NUM, cfg.mode, 0, 0, 0);
    if (install_err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "I2C ya inicializado en puerto %d, reutilizando config actual", (int)I2C_PORT_NUM);
    } else if (install_err == ESP_FAIL) {
        ESP_LOGW(TAG, "i2c_driver_install accel retorno ESP_FAIL; intentando operar con bus existente");
    } else if (install_err == ESP_OK) {
        ESP_LOGI(TAG, "I2C accel instalado en puerto %d", (int)I2C_PORT_NUM);
    } else {
        ESP_LOGE(TAG, "No se pudo inicializar I2C para accel: %s", esp_err_to_name(install_err));
        return install_err;
    }

    i2c_scan_log();

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
        if (i2c_ping(0x15) == ESP_OK) {
            ESP_LOGW(TAG, "Detectado touch (0x15) en bus I2C, pero QMI8658 no responde.");
        }
        ESP_LOGW(TAG, "No se detecto QMI8658 en I2C (SDA=%d, SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = accel_write_reg(REG_CTRL1, 0x40); // auto increment
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
