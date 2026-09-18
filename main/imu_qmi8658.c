#include "imu_qmi8658.h"
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "board_i2c.h"

static const char *TAG = "IMU";

#define QMI_ADDR_1      0x6B
#define QMI_ADDR_2      0x6A

#define REG_WHOAMI      0x00
#define QMI_WHOAMI_VALUE 0x05
#define REG_CTRL1       0x02
#define REG_CTRL2       0x03
#define REG_CTRL7       0x08
#define REG_CTRL8       0x09
#define REG_AX_L        0x35

static uint8_t s_addr = 0;

static esp_err_t rd(uint8_t addr, uint8_t reg, void *buf, size_t len)
{
    return board_i2c_read_reg(addr, reg, buf, len, 100);
}

static esp_err_t wr(uint8_t addr, uint8_t reg, uint8_t val)
{
    return board_i2c_write_reg(addr, reg, val, 100);
}

static bool probe_addr(uint8_t addr, uint8_t *who)
{
    uint8_t v = 0;
    if (rd(addr, REG_WHOAMI, &v, 1) == ESP_OK && v == QMI_WHOAMI_VALUE) {
        if (who) *who = v;
        return true;
    }
    return false;
}

esp_err_t imu_qmi8658_init(void)
{
    esp_err_t result = board_i2c_init();
    if (result != ESP_OK) return result;

    uint8_t who = 0;
    if (probe_addr(QMI_ADDR_1, &who)) s_addr = QMI_ADDR_1;
    else if (probe_addr(QMI_ADDR_2, &who)) s_addr = QMI_ADDR_2;
    else {
        ESP_LOGE(TAG, "No encuentro QMI8658 en 0x6B/0x6A. (I2C pins?)");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QMI8658 detectado addr=0x%02X WHOAMI=0x%02X", s_addr, who);

    static const struct {
        uint8_t reg;
        uint8_t value;
    } init_sequence[] = {
        {REG_CTRL1, (1U << 6)},
        {REG_CTRL8, (1U << 7)},
        {REG_CTRL7, 0x00},
        {REG_CTRL2, 0x06},
        {REG_CTRL7, 0x01},
    };

    for (size_t i = 0; i < sizeof(init_sequence) / sizeof(init_sequence[0]); i++) {
        result = wr(s_addr, init_sequence[i].reg, init_sequence[i].value);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Fallo configurando reg 0x%02X: %s",
                     init_sequence[i].reg, esp_err_to_name(result));
            s_addr = 0;
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t imu_qmi8658_read_accel(imu_accel_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_addr) return ESP_ERR_INVALID_STATE;

    uint8_t b[6];
    esp_err_t r = rd(s_addr, REG_AX_L, b, sizeof(b));
    if (r != ESP_OK) {
        static uint32_t s_rd_err_cnt = 0;
        s_rd_err_cnt++;
        if ((s_rd_err_cnt % 10U) == 1U) {
            ESP_LOGW(TAG, "imu_qmi8658_read_accel timeout/error (r=0x%x, cnt=%lu)",
                     (unsigned)r, (unsigned long)s_rd_err_cnt);
        }
        return r;
    }

    int16_t rx = (int16_t)((b[1] << 8) | b[0]);
    int16_t ry = (int16_t)((b[3] << 8) | b[2]);
    int16_t rz = (int16_t)((b[5] << 8) | b[4]);

    const float scale = 1.0f / 16384.0f;
    out->ax = rx * scale;
    out->ay = ry * scale;
    out->az = rz * scale;
    return ESP_OK;
}
