#include <math.h>
#include <stdbool.h>
#include <stdatomic.h>
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
#include "nvs_flash.h"

#include "display_st7789_lvgl.h"
#include "ui_clock.h"
#include "imu_qmi8658.h"

static const char *TAG = "IAWICHU";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRIES   8

static EventGroupHandle_t s_wifi_event_group = NULL;
static atomic_int s_wifi_retry_num = 0;
static bool s_wifi_started = false;
static char s_wifi_ssid[33] = {0};
static char s_wifi_pass[65] = {0};

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t d = lv_timer_handler();
        if (d == LV_NO_TIMER_READY || d > 20U) d = 20U;
        if (d < 5) d = 5;
        TickType_t ticks = pdMS_TO_TICKS(d);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

static disp_rot_t rot_from_accel(float ax, float ay)
{
    const float TH = 0.55f;
    if (fabsf(ay) > fabsf(ax)) {
        if (ay > TH)  return DISP_ROT_180;
        if (ay < -TH) return DISP_ROT_0;
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
                display_st7789_request_rotation(cur);
                ESP_LOGI(TAG, "ROT solicitada=%d (ax=%.2f ay=%.2f az=%.2f)",
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
        atomic_store_explicit(&s_wifi_retry_num, 0, memory_order_release);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        if (esp_wifi_connect() != ESP_OK) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        int retry_count = atomic_load_explicit(&s_wifi_retry_num, memory_order_acquire);
        if (retry_count < WIFI_MAX_RETRIES) {
            esp_err_t result = esp_wifi_connect();
            if (result == ESP_OK) {
                int attempt = atomic_fetch_add_explicit(&s_wifi_retry_num, 1, memory_order_acq_rel) + 1;
                ESP_LOGW(TAG, "WiFi reconectando (%d/%d)", attempt, WIFI_MAX_RETRIES);
            } else {
                ESP_LOGE(TAG, "WiFi reconnect falló: %s", esp_err_to_name(result));
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi no pudo conectar");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        atomic_store_explicit(&s_wifi_retry_num, 0, memory_order_release);
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t app_nvs_init_once(void)
{
    static bool s_nvs_ready = false;
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_nvs_ready = true;
    }
    return err;
}

static esp_err_t wifi_connect_blocking(void)
{
    if (strlen(s_wifi_ssid) == 0) {
        ESP_LOGW(TAG, "SSID vacío: configura CONFIG_IAWICHU_WIFI_SSID");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = app_nvs_init_once();
    if (result != ESP_OK) return result;

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
        if (!s_wifi_event_group) return ESP_ERR_NO_MEM;
    }

    static bool s_netif_inited = false;
    if (!s_netif_inited) {
        result = esp_netif_init();
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
        esp_err_t evr = esp_event_loop_create_default();
        if (evr != ESP_OK && evr != ESP_ERR_INVALID_STATE) {
            return evr;
        }
        if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
        s_netif_inited = true;
    }

    static bool s_wifi_driver_inited = false;
    if (!s_wifi_driver_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t wr = esp_wifi_init(&cfg);
        if (wr != ESP_OK && wr != ESP_ERR_INVALID_STATE) {
            return wr;
        }
        s_wifi_driver_inited = true;
    }

    static bool s_wifi_handlers_registered = false;
    if (!s_wifi_handlers_registered) {
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        result = esp_event_handler_instance_register(WIFI_EVENT,
                                                     ESP_EVENT_ANY_ID,
                                                     &wifi_event_handler,
                                                     NULL,
                                                     &instance_any_id);
        if (result != ESP_OK) return result;
        result = esp_event_handler_instance_register(IP_EVENT,
                                                     IP_EVENT_STA_GOT_IP,
                                                     &wifi_event_handler,
                                                     NULL,
                                                     &instance_got_ip);
        if (result != ESP_OK) return result;
        s_wifi_handlers_registered = true;
    }

    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) return result;
    result = wifi_apply_runtime_config();
    if (result != ESP_OK) return result;
    result = esp_wifi_start();
    if (result != ESP_OK) return result;
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

static bool app_sntp_sync_time(void)
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
        EventBits_t bits = s_wifi_event_group ? xEventGroupGetBits(s_wifi_event_group) : 0;
        if (!(bits & WIFI_CONNECTED_BIT)) {
            esp_sntp_stop();
            return false;
        }
        ESP_LOGI(TAG, "Esperando hora NTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    char strftime_buf[64];
    if (timeinfo.tm_year >= (2024 - 1900)) {
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Hora sincronizada: %s", strftime_buf);
        return true;
    } else {
        ESP_LOGW(TAG, "No se pudo sincronizar NTP");
        return false;
    }
}

static void wifi_reconnect_and_sync_task(void *arg)
{
    (void)arg;
    bool time_synced = false;

    while (1) {
        if (strlen(s_wifi_ssid) == 0) {
            ui_clock_set_network_state(false, false);
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        if (!s_wifi_started) {
            wifi_connect_blocking();
        } else {
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            if (!(bits & WIFI_CONNECTED_BIT) && (bits & WIFI_FAIL_BIT)) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
                time_synced = false;
                atomic_store_explicit(&s_wifi_retry_num, 0, memory_order_release);
                esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "WiFi nuevo ciclo falló: %s", esp_err_to_name(result));
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                EventBits_t retry_bits = xEventGroupWaitBits(s_wifi_event_group,
                                                             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                             pdFALSE,
                                                             pdFALSE,
                                                             pdMS_TO_TICKS(12000));
                (void)retry_bits;
            }
        }

        EventBits_t bits_now = s_wifi_event_group ? xEventGroupGetBits(s_wifi_event_group) : 0;
        bool connected = (bits_now & WIFI_CONNECTED_BIT) != 0;
        if (!connected) {
            time_synced = false;
            ui_clock_set_network_state(false, false);
        } else if (!time_synced) {
            ui_clock_set_network_state(true, false);
            bool synced = app_sntp_sync_time();
            EventBits_t bits_after = xEventGroupGetBits(s_wifi_event_group);
            connected = (bits_after & WIFI_CONNECTED_BIT) != 0;
            time_synced = connected && synced;
            ui_clock_set_network_state(connected, time_synced);
        } else {
            ui_clock_set_network_state(true, true);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ui_gesture_request_handler(const gesture_profile_t *profile)
{
    if (!profile) return;
    ESP_LOGI(TAG, "Gesto preview %s (%s), sin salida a motores", profile->name, profile->code);
}

void app_main(void)
{
    setenv("TZ", CONFIG_IAWICHU_TZ, 1);
    tzset();

    lv_display_t *d = display_st7789_lvgl_init();
    if (!d) return;

    /* Orientación física inicial; las rotaciones posteriores se encolan a LVGL. */
    display_st7789_set_rotation(DISP_ROT_180);

    ui_clock_create();
    ui_clock_set_gesture_request_callback(ui_gesture_request_handler);

    wifi_fill_runtime_from_config();

    if (xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo iniciar la tarea LVGL");
        return;
    }
    if (xTaskCreate(imu_task, "imu", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Sin tarea IMU; la interfaz continúa en orientación fija");
        ui_clock_set_accel(0, 0, false);
    }
    if (strlen(s_wifi_ssid) > 0) {
        if (xTaskCreate(wifi_reconnect_and_sync_task, "wifi_ntp", 6144, NULL, 4, NULL) != pdPASS) {
            ESP_LOGW(TAG, "Sin tarea WiFi/NTP; reloj en modo local/uptime");
            ui_clock_set_network_state(false, false);
        }
    } else {
        ESP_LOGI(TAG, "WiFi sin configurar; reloj en modo local/uptime");
    }

    ESP_LOGI(TAG, "OK: NERVE OS + touch + auto-rotacion segura + WiFi/NTP.");
}
