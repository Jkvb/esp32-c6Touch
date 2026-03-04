#include "ui_clock.h"
#include "lvgl.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

#if defined(LV_FONT_MONTSERRAT_48) && (LV_FONT_MONTSERRAT_48)
  #define CLOCK_FONT (&lv_font_montserrat_48)
#elif defined(LV_FONT_MONTSERRAT_32) && (LV_FONT_MONTSERRAT_32)
  #define CLOCK_FONT (&lv_font_montserrat_32)
#else
  #define CLOCK_FONT (LV_FONT_DEFAULT)
#endif

static const char *TAG_UI = "UI_CLOCK";

static lv_obj_t *s_lbl = NULL;
static volatile int16_t s_ax = 0;
static volatile int16_t s_ay = 0;
static volatile bool s_accel_valid = false;

static lv_obj_t *s_drawer = NULL;
static lv_obj_t *s_menu_card = NULL;
static lv_obj_t *s_home_page = NULL;
static lv_obj_t *s_wifi_page = NULL;
static lv_obj_t *s_ta_ssid = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_kb = NULL;
static ui_wifi_save_cb_t s_wifi_cb = NULL;

static int16_t clamp16(int32_t v, int16_t min, int16_t max)
{
    if (v < min) return min;
    if (v > max) return max;
    return (int16_t)v;
}

static void hide_drawer(void)
{
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    if (s_kb) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG_UI, "Drawer oculto");
}

static void show_home_page(void)
{
    if (s_home_page) lv_obj_clear_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
    if (s_wifi_page) lv_obj_add_flag(s_wifi_page, LV_OBJ_FLAG_HIDDEN);
    if (s_kb) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void show_wifi_page(void)
{
    if (s_home_page) lv_obj_add_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
    if (s_wifi_page) lv_obj_clear_flag(s_wifi_page, LV_OBJ_FLAG_HIDDEN);
}

static void show_drawer(void)
{
    if (!s_drawer) return;
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG_UI, "Drawer abierto");
    show_home_page();
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    char buf[40];
    if (ti.tm_year < (2024 - 1900)) {
        snprintf(buf, sizeof(buf), "wichIA\n--:--:--");
    } else {
        snprintf(buf, sizeof(buf), "wichIA\n%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    lv_label_set_text(s_lbl, buf);

    if (s_accel_valid) {
        int16_t xoff = clamp16((int32_t)s_ax / 500, -50, 50);
        int16_t yoff = clamp16((int32_t)(-s_ay) / 500, -70, 70);
        lv_obj_align(s_lbl, LV_ALIGN_CENTER, xoff, yoff);

        uint8_t blue = (uint8_t)clamp16(80 + (s_ax > 0 ? s_ax : -s_ax) / 100, 40, 200);
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_make(0, 40, blue), 0);
    }
}

static void btn_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "Tap en cerrar menu");
    hide_drawer();
}

static void kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_focus_cb(lv_event_t *e)
{
    if (!s_kb) return;
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG_UI, "Focus en input WiFi");
}

static void btn_save_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);

    ESP_LOGI(TAG_UI, "Guardar WiFi desde UI (ssid_len=%d pass_len=%d)", (int)strlen(ssid), (int)strlen(pass));
    if (s_wifi_cb) {
        s_wifi_cb(ssid, pass);
    } else {
        ESP_LOGW(TAG_UI, "s_wifi_cb es NULL, no se aplican credenciales");
    }
    hide_drawer();
}

static void btn_open_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "Tap en abrir menu (W/handle)");
    show_drawer();
}

static void btn_wifi_app_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "Tap app WiFi");
    show_wifi_page();
}

static void btn_back_home_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "Back a apps");
    show_home_page();
}

static void screen_gesture_open_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    ESP_LOGI(TAG_UI, "Gesture detectado dir=%d", (int)dir);
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) {
        show_drawer();
    }
}

void ui_clock_create(void)
{
    lv_obj_t *scr = lv_screen_active();

    ESP_LOGI(TAG_UI, "ui_clock_create init");
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x002060), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, screen_gesture_open_cb, LV_EVENT_GESTURE, NULL);

    s_lbl = lv_label_create(scr);
    lv_label_set_text(s_lbl, "wichIA\n--:--:--");

    lv_obj_set_style_text_font(s_lbl, CLOCK_FONT, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_lbl);

    lv_obj_t *btn_open = lv_button_create(scr);
    lv_obj_set_size(btn_open, 36, 36);
    lv_obj_align(btn_open, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(btn_open, btn_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *open_lbl = lv_label_create(btn_open);
    lv_label_set_text(open_lbl, "W");
    lv_obj_center(open_lbl);

    lv_obj_t *btn_swipe_hint = lv_button_create(scr);
    lv_obj_set_size(btn_swipe_hint, 14, 90);
    lv_obj_align(btn_swipe_hint, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_swipe_hint, LV_OPA_40, 0);
    lv_obj_set_style_radius(btn_swipe_hint, 0, 0);
    lv_obj_add_event_cb(btn_swipe_hint, btn_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint_lbl = lv_label_create(btn_swipe_hint);
    lv_label_set_text(hint_lbl, "≡");
    lv_obj_center(hint_lbl);

    /* Smartwatch-like drawer */
    s_drawer = lv_obj_create(scr);
    lv_obj_set_size(s_drawer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_drawer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);

    s_menu_card = lv_obj_create(s_drawer);
    lv_obj_set_size(s_menu_card, 228, 282);
    lv_obj_center(s_menu_card);
    lv_obj_set_style_radius(s_menu_card, 16, 0);

    lv_obj_t *title = lv_label_create(s_menu_card);
    lv_label_set_text(title, "Smart Menu");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *btn_close = lv_button_create(s_menu_card);
    lv_obj_set_size(btn_close, 34, 28);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_add_event_cb(btn_close, btn_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(btn_close);
    lv_label_set_text(close_lbl, "X");
    lv_obj_center(close_lbl);

    s_home_page = lv_obj_create(s_menu_card);
    lv_obj_set_size(s_home_page, 210, 220);
    lv_obj_align(s_home_page, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_border_width(s_home_page, 0, 0);

    lv_obj_t *home_txt = lv_label_create(s_home_page);
    lv_label_set_text(home_txt, "Apps");
    lv_obj_align(home_txt, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t *btn_wifi_app = lv_button_create(s_home_page);
    lv_obj_set_size(btn_wifi_app, 92, 92);
    lv_obj_align(btn_wifi_app, LV_ALIGN_TOP_LEFT, 4, 24);
    lv_obj_add_event_cb(btn_wifi_app, btn_wifi_app_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_app_lbl = lv_label_create(btn_wifi_app);
    lv_label_set_text(wifi_app_lbl, LV_SYMBOL_WIFI "\nWiFi");
    lv_obj_center(wifi_app_lbl);

    lv_obj_t *btn_clock_app = lv_button_create(s_home_page);
    lv_obj_set_size(btn_clock_app, 92, 92);
    lv_obj_align(btn_clock_app, LV_ALIGN_TOP_RIGHT, -4, 24);
    lv_obj_add_event_cb(btn_clock_app, btn_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clock_app_lbl = lv_label_create(btn_clock_app);
    lv_label_set_text(clock_app_lbl, LV_SYMBOL_HOME "\nReloj");
    lv_obj_center(clock_app_lbl);

    s_wifi_page = lv_obj_create(s_menu_card);
    lv_obj_set_size(s_wifi_page, 210, 220);
    lv_obj_align(s_wifi_page, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_border_width(s_wifi_page, 0, 0);

    lv_obj_t *wifi_title = lv_label_create(s_wifi_page);
    lv_label_set_text(wifi_title, LV_SYMBOL_WIFI " Config WiFi");
    lv_obj_align(wifi_title, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t *btn_back = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_back, 56, 26);
    lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, -2, -2);
    lv_obj_add_event_cb(btn_back, btn_back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(btn_back);
    lv_label_set_text(back_lbl, "Apps");
    lv_obj_center(back_lbl);

    s_ta_ssid = lv_textarea_create(s_wifi_page);
    lv_obj_set_width(s_ta_ssid, 196);
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_MID, 0, 28);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_obj_add_event_cb(s_ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    s_ta_pass = lv_textarea_create(s_wifi_page);
    lv_obj_set_width(s_ta_pass, 196);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 74);
    lv_textarea_set_placeholder_text(s_ta_pass, "Password");
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_obj_add_event_cb(s_ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_save = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_save, 94, 34);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 4, -8);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(btn_save);
    lv_label_set_text(save_lbl, "Guardar");
    lv_obj_center(save_lbl);

    lv_obj_t *btn_cancel = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_cancel, 94, 34);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -4, -8);
    lv_obj_add_event_cb(btn_cancel, btn_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_lbl, "Cerrar");
    lv_obj_center(cancel_lbl);

    s_kb = lv_keyboard_create(s_drawer);
    lv_obj_set_size(s_kb, lv_pct(100), 120);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    hide_drawer();
    show_home_page();

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
}

void ui_clock_prefill_wifi(const char *ssid, const char *pass)
{
    if (s_ta_ssid && ssid) lv_textarea_set_text(s_ta_ssid, ssid);
    if (s_ta_pass && pass) lv_textarea_set_text(s_ta_pass, pass);
}
