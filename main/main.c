#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"

#include "display_st7789_lvgl.h"
#include "ui_clock.h"
#include "imu_qmi8658.h"

static const char *TAG = "IAWICHU";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRIES   8

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_wifi_retry_num = 0;
static bool s_wifi_started = false;
static char s_wifi_ssid[33] = {0};
static char s_wifi_pass[65] = {0};

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
            int16_t ax_lsb = (int16_t)(a.ax * 16384.0f);
            int16_t ay_lsb = (int16_t)(a.ay * 16384.0f);
            ui_clock_set_accel(ax_lsb, ay_lsb, true);

            if (r == cand) stable++;
            else { cand = r; stable = 0; }

            if (stable >= 3 && cand != cur) {
                cur = cand;
                ESP_LOGI(TAG, "ROT detectada=%d (ax=%.2f ay=%.2f az=%.2f) [auto-rot desactivada]",
                         (int)cur, a.ax, a.ay, a.az);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            ui_clock_set_accel(0, 0, false);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_retry_num = 0;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRIES) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGW(TAG, "WiFi reconectando (%d/%d)", s_wifi_retry_num, WIFI_MAX_RETRIES);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi no pudo conectar");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect_blocking(void)
{
    if (strlen(s_wifi_ssid) == 0) {
        ESP_LOGW(TAG, "SSID vacío: configura CONFIG_IAWICHU_WIFI_SSID");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_event_group) s_wifi_event_group = xEventGroupCreate();

    static bool s_netif_inited = false;
    if (!s_netif_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        s_netif_inited = true;
    }

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

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    if (bits & WIFI_FAIL_BIT) return ESP_FAIL;
    return ESP_ERR_TIMEOUT;
}

static void app_sntp_sync_time(void)
{
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_IAWICHU_NTP_SERVER);
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2024 - 1900) && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Esperando hora NTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    setenv("TZ", CONFIG_IAWICHU_TZ, 1);
    tzset();

    char strftime_buf[64];
    if (timeinfo.tm_year >= (2024 - 1900)) {
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Hora sincronizada: %s", strftime_buf);
    } else {
        ESP_LOGW(TAG, "No se pudo sincronizar NTP");
    }
}

static void wifi_reconnect_and_sync_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_wifi_started) {
            if (wifi_connect_blocking() == ESP_OK) {
                app_sntp_sync_time();
            }
        } else {
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            if (!(bits & WIFI_CONNECTED_BIT)) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
                esp_wifi_disconnect();
                esp_wifi_connect();
                EventBits_t retry_bits = xEventGroupWaitBits(s_wifi_event_group,
                                                             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                             pdFALSE,
                                                             pdFALSE,
                                                             pdMS_TO_TICKS(12000));
                if (retry_bits & WIFI_CONNECTED_BIT) {
                    app_sntp_sync_time();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

static void ui_wifi_save_handler(const char *ssid, const char *pass)
{
    if (!ssid) return;
    strlcpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid));
    strlcpy(s_wifi_pass, pass ? pass : "", sizeof(s_wifi_pass));

    ESP_LOGI(TAG, "Credenciales WiFi actualizadas desde UI (ssid=%s)", s_wifi_ssid);

    if (s_wifi_started) {
        ESP_ERROR_CHECK(wifi_apply_runtime_config());
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
}

void app_main(void)
{
    lv_display_t *d = display_st7789_lvgl_init();
    if (!d) return;

    /* Pantalla fija volteada para evitar conflictos de rotación dinámica */
    display_st7789_set_rotation(DISP_ROT_180);

    ui_clock_create();

    wifi_fill_runtime_from_config();
    ui_clock_prefill_wifi(s_wifi_ssid, s_wifi_pass);
    ui_clock_set_wifi_callback(ui_wifi_save_handler);

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL);
    xTaskCreate(imu_task,  "imu",  3072, NULL, 4, NULL);
    xTaskCreate(wifi_reconnect_and_sync_task, "wifi_ntp", 6144, NULL, 4, NULL);

    ESP_LOGI(TAG, "OK: reloj + touch + pantalla fija + WiFi/NTP.");
}
