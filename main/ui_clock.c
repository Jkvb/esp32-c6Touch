#include "ui_clock.h"
#include "lvgl.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(LV_FONT_MONTSERRAT_28) && (LV_FONT_MONTSERRAT_28)
  #define CLOCK_FONT (&lv_font_montserrat_28)
#elif defined(LV_FONT_MONTSERRAT_24) && (LV_FONT_MONTSERRAT_24)
  #define CLOCK_FONT (&lv_font_montserrat_24)
#elif defined(LV_FONT_MONTSERRAT_32) && (LV_FONT_MONTSERRAT_32)
  #define CLOCK_FONT (&lv_font_montserrat_32)
#elif defined(LV_FONT_MONTSERRAT_40) && (LV_FONT_MONTSERRAT_40)
  #define CLOCK_FONT (&lv_font_montserrat_40)
#else
  #define CLOCK_FONT (LV_FONT_DEFAULT)
#endif

static const char *TAG_UI = "UI_CLOCK";

static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tiles[5] = {0};
static lv_obj_t *s_brand_lbl = NULL;
static lv_obj_t *s_time_lbl = NULL;

static volatile int16_t s_ax = 0;
static volatile int16_t s_ay = 0;
static volatile bool s_accel_valid = false;

static ui_wifi_save_cb_t s_wifi_cb = NULL;
static ui_wifi_scan_cb_t s_wifi_scan_cb = NULL;

static int16_t clamp16(int32_t v, int16_t min, int16_t max)
{
    if (v < min) return min;
    if (v > max) return max;
    return (int16_t)v;
}

static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    for (int i = 0; i < 5; i++) {
        if (act == s_tiles[i]) {
            ESP_LOGI(TAG_UI, "Pantalla activa=%d (touch swipe)", i + 1);
            break;
        }
    }
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    char buf[16];
    if (ti.tm_year < (2024 - 1900)) {
        snprintf(buf, sizeof(buf), "--:--:--");
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    lv_label_set_text(s_time_lbl, buf);

    uint8_t sec = (uint8_t)ti.tm_sec;
    uint8_t green = (uint8_t)(130 + (sec * 2) % 110);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_make(20, green, 25), 0);
    lv_obj_set_style_text_color(s_brand_lbl, lv_color_make(20, (uint8_t)(green - 40), 20), 0);

    if (s_accel_valid) {
        int16_t xoff = clamp16((int32_t)s_ax / 450, -26, 26);
        int16_t yoff = clamp16((int32_t)(-s_ay) / 450, -32, 32);
        lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, xoff, yoff + 6);
        lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, xoff / 2, 18);
    } else {
        lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);
        lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 18);
    }
}

static void create_info_tile(lv_obj_t *tile, const char *title, const char *line1, const char *line2)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);

    lv_obj_t *ttl = lv_label_create(tile);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_color(ttl, lv_color_hex(0x00ff66), 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *l1 = lv_label_create(tile);
    lv_label_set_text(l1, line1);
    lv_obj_set_style_text_color(l1, lv_color_hex(0x00cc44), 0);
    lv_obj_align(l1, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *l2 = lv_label_create(tile);
    lv_label_set_text(l2, line2);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x00aa33), 0);
    lv_obj_align(l2, LV_ALIGN_CENTER, 0, 16);
}

void ui_clock_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    ESP_LOGI(TAG_UI, "ui_clock_create init (modo 5 pantallas)");

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_tileview, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_add_event_cb(s_tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    for (uint8_t i = 0; i < 5; i++) {
        s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
    }

    /* Pantalla 1: Reloj adaptado a 170x320 */
    lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_tiles[0], 0, 0);

    s_brand_lbl = lv_label_create(s_tiles[0]);
    lv_label_set_text(s_brand_lbl, "wichIA");
    lv_obj_set_style_text_letter_space(s_brand_lbl, 2, 0);
    lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x00cc44), 0);
    lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 18);

    s_time_lbl = lv_label_create(s_tiles[0]);
    lv_label_set_text(s_time_lbl, "--:--:--");
    lv_obj_set_style_text_font(s_time_lbl, CLOCK_FONT, 0);
    lv_obj_set_style_text_align(s_time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_time_lbl, lv_pct(98)); /* mejor adaptación horizontal */
    lv_obj_set_style_pad_left(s_time_lbl, 2, 0);
    lv_obj_set_style_pad_right(s_time_lbl, 2, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *hint = lv_label_create(s_tiles[0]);
    lv_label_set_text(hint, "Desliza para ver pantallas 2-5");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x007722), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Pantallas 2..5 */
    create_info_tile(s_tiles[1], "Pantalla 2", "Estado sensores", "(maqueta touch)");
    create_info_tile(s_tiles[2], "Pantalla 3", "Estado WiFi", "(maqueta touch)");
    create_info_tile(s_tiles[3], "Pantalla 4", "Estado reloj", "(maqueta touch)");
    create_info_tile(s_tiles[4], "Pantalla 5", "Debug", "(maqueta touch)");

    lv_tileview_set_tile_by_index(s_tileview, 0, 0, LV_ANIM_OFF);
    lv_timer_create(clock_timer_cb, 200, NULL);
}

void ui_clock_set_accel(int16_t x, int16_t y, bool valid)
{
    s_ax = x;
    s_ay = y;
    s_accel_valid = valid;
}

void ui_clock_set_wifi_callback(ui_wifi_save_cb_t cb)
{
    s_wifi_cb = cb;
    ESP_LOGI(TAG_UI, "Callback WiFi set (modo simple): %s", s_wifi_cb ? "OK" : "NULL");
}

void ui_clock_set_wifi_scan_callback(ui_wifi_scan_cb_t cb)
{
    s_wifi_scan_cb = cb;
    ESP_LOGI(TAG_UI, "Callback Scan set (modo simple): %s", s_wifi_scan_cb ? "OK" : "NULL");
}

void ui_clock_set_scan_results(const char *options_newline)
{
    (void)options_newline;
    ESP_LOGI(TAG_UI, "Scan results recibidos (modo simple, sin formulario WiFi)");
}

void ui_clock_prefill_wifi(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    ESP_LOGI(TAG_UI, "Prefill WiFi recibido (modo simple, sin formulario WiFi)");
}
