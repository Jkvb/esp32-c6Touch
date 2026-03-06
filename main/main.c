#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "display_st7789_lvgl.h"
#include "ui_clock.h"
#include "imu_qmi8658.h"

static const char *TAG = "IAWICHU";

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t d = lv_timer_handler();
        if (d < 5) d = 5;
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

static disp_rot_t rot_from_accel(float ax, float ay)
{
    const float TH = 0.55f;
    if (fabsf(ay) > fabsf(ax)) {
        if (ay > TH)  return DISP_ROT_0;
        if (ay < -TH) return DISP_ROT_180;
    } else {
        if (ax > TH)  return DISP_ROT_270;
        if (ax < -TH) return DISP_ROT_90;
    }
    return display_st7789_get_rotation();
}

static void imu_task(void *arg)
{
    (void)arg;

    if (imu_qmi8658_init() != ESP_OK) {
        ESP_LOGE(TAG, "IMU no inicializó (I2C?)");
        vTaskDelete(NULL);
        return;
    }

    disp_rot_t cur = display_st7789_get_rotation();
    disp_rot_t cand = cur;
    int stable = 0;

    while (1) {
        imu_accel_t a;
        if (imu_qmi8658_read_accel(&a) == ESP_OK) {
            disp_rot_t r = rot_from_accel(a.ax, a.ay);

            if (r == cand) stable++;
            else { cand = r; stable = 0; }

            if (stable >= 3 && cand != cur) {
                cur = cand;
                ESP_LOGI(TAG, "ROT detectada=%d (ax=%.2f ay=%.2f az=%.2f) [auto-rot desactivada]",
                         (int)cur, a.ax, a.ay, a.az);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

void app_main(void)
{
    lv_display_t *d = display_st7789_lvgl_init();
    if (!d) return;

    /* Pantalla fija volteada para evitar conflictos de rotación dinámica */
    display_st7789_set_rotation(DISP_ROT_180);

    ui_clock_create();

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL);
    xTaskCreate(imu_task,  "imu",  3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "OK: reloj + touch + pantalla fija (WiFi después).");
}
