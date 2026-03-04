#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "display_st7789_lvgl.h"
#include "ui_clock.h"
#include "accel_qmi8658.h"
#include "lvgl.h"

static const char *TAG = "IAWICHU";

/* Backlight pin (según Waveshare): GPIO15 */
#define PIN_NUM_LCD_BL 15
/* Este módulo quedó estable con BL activo-bajo (0=ON). */
#define LCD_BL_ON_LEVEL 0

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_wifi_started = false;
static char s_wifi_ssid[sizeof(((wifi_sta_config_t *)0)->ssid)] = {0};
static char s_wifi_pass[sizeof(((wifi_sta_config_t *)0)->password)] = {0};

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void accel_task(void *arg)
{
    (void)arg;

    while (1) {
        int16_t x = 0, y = 0, z = 0;
        esp_err_t err = accel_qmi8658_read_xyz(&x, &y, &z);
        if (err == ESP_OK) {
            ui_clock_set_accel(x, y, true);
        } else {
            ui_clock_set_accel(0, 0, false);
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static void backlight_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    gpio_set_level(PIN_NUM_LCD_BL, LCD_BL_ON_LEVEL);
}

static void backlight_keep_on_task(void *arg)
{
    (void)arg;
    while (1) {
        gpio_set_level(PIN_NUM_LCD_BL, LCD_BL_ON_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reintentando WiFi (%d/10)", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi OK, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "EVENTO_WIFI_CONECTADO ssid='%s'", s_wifi_ssid);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_fill_runtime_from_config(void)
{
    strlcpy(s_wifi_ssid, CONFIG_IAWICHU_WIFI_SSID, sizeof(s_wifi_ssid));
    strlcpy(s_wifi_pass, CONFIG_IAWICHU_WIFI_PASS, sizeof(s_wifi_pass));
}

static esp_err_t wifi_apply_runtime_config(void)
{
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, s_wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_wifi_pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static esp_err_t wifi_connect_blocking(void)
{
    if (strlen(s_wifi_ssid) == 0) {
        ESP_LOGW(TAG, "SSID vacío. Configúralo en panel lateral o en menuconfig.");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(wifi_apply_runtime_config());
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    ESP_LOGI(TAG, "Conectando a WiFi SSID='%s'...", s_wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void sntp_sync_time(void)
{
    ESP_LOGI(TAG, "Iniciando SNTP (%s)", CONFIG_IAWICHU_NTP_SERVER);
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_IAWICHU_NTP_SERVER);
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 20;

    while (timeinfo.tm_year < (2024 - 1900) && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Esperando hora por SNTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    setenv("TZ", CONFIG_IAWICHU_TZ, 1);
    tzset();

    if (timeinfo.tm_year >= (2024 - 1900)) {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Hora sincronizada: %s (TZ=%s)", strftime_buf, CONFIG_IAWICHU_TZ);
        ESP_LOGI(TAG, "EVENTO_FECHA_ACTUALIZADA epoch=%lld", (long long)now);
    } else {
        ESP_LOGW(TAG, "No se pudo sincronizar hora SNTP.");
    }
}

static void wifi_reconnect_and_sync_task(void *arg)
{
    (void)arg;

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_disconnect();
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        sntp_sync_time();
    } else {
        ESP_LOGW(TAG, "No se pudo conectar con credenciales guardadas desde UI.");
    }

    vTaskDelete(NULL);
}

static void ui_wifi_save_handler(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "Guardar WiFi ignorado: SSID vacío.");
        return;
    }

    strlcpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid));
    strlcpy(s_wifi_pass, pass ? pass : "", sizeof(s_wifi_pass));

    ESP_LOGI(TAG, "Credenciales WiFi actualizadas desde UI. SSID='%s'", s_wifi_ssid);

    if (!s_wifi_started) {
        ESP_LOGW(TAG, "WiFi aún no inicializado; se usarán estas credenciales al iniciar WiFi.");
        return;
    }

    esp_err_t err = wifi_apply_runtime_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error aplicando config WiFi desde UI: %s", esp_err_to_name(err));
        return;
    }

    xTaskCreate(wifi_reconnect_and_sync_task, "wifi_reconnect_sync", 4096, NULL, 4, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boot. DISPLAY + reloj + acelerómetro + WiFi SNTP.");

    wifi_fill_runtime_from_config();

    backlight_init();
    ESP_LOGI(TAG, "Backlight forzado ON en GPIO%d (nivel=%d)", PIN_NUM_LCD_BL, LCD_BL_ON_LEVEL);

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
    ui_clock_set_wifi_callback(ui_wifi_save_handler);
    ui_clock_prefill_wifi(s_wifi_ssid, s_wifi_pass);

    if (wifi_connect_blocking() == ESP_OK) {
        sntp_sync_time();
    } else {
        ESP_LOGW(TAG, "Sin WiFi: reloj quedará en --:--:-- hasta sincronizar tiempo.");
    }

    esp_err_t acc_ret = accel_qmi8658_init();
    if (acc_ret == ESP_OK) {
        ESP_LOGI(TAG, "Acelerómetro activo en I2C SDA=GPIO20 SCL=GPIO21.");
        xTaskCreate(accel_task, "accel_task", 4096, NULL, 4, NULL);
    } else {
        ESP_LOGW(TAG, "Sin acelerómetro por ahora. Revisa puertos I2C: SDA=GPIO20 SCL=GPIO21.");
    }

    xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
    xTaskCreate(backlight_keep_on_task, "backlight_keep", 2048, NULL, 3, NULL);

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
