#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "display_st7789_lvgl.h"
#include "ui_clock.h"
#include "lvgl.h"

static const char *TAG = "IAWICHU";

/* Backlight pin (segÃºn Waveshare): GPIO15 */
#define PIN_NUM_LCD_BL 15

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void backlight_blink_test(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    // 6 parpadeos rÃ¡pidos (si ves esto, BL pin estÃ¡ bien)
    for (int i = 0; i < 6; i++) {
        gpio_set_level(PIN_NUM_LCD_BL, (i & 1) ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    // dejarlo en 1 por defecto (si tu BL es activo-bajo, luego lo cambiamos)
    gpio_set_level(PIN_NUM_LCD_BL, 0); }

void app_main(void)
{
    ESP_LOGI(TAG, "Boot. Solo DISPLAY (sin WiFi).");

    backlight_blink_test();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    lv_display_t *disp = display_st7789_lvgl_init();
    if (!disp) {
        ESP_LOGE(TAG, "display_st7789_lvgl_init() fallo.");
        return;
    }

    ui_clock_create();

    xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Si el BL parpadeo pero sigues sin ver nada, ajustamos INVERT/MIRROR/BL polarity.");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}