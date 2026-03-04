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

static lv_obj_t *s_brand_lbl = NULL;
static lv_obj_t *s_time_lbl = NULL;
static volatile int16_t s_ax = 0;
static volatile int16_t s_ay = 0;
static volatile bool s_accel_valid = false;

/* Drawer menu */
static lv_obj_t *s_drawer = NULL;
static lv_obj_t *s_menu_card = NULL;
static lv_obj_t *s_home_page = NULL;
static lv_obj_t *s_wifi_page = NULL;
static lv_obj_t *s_ta_ssid = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_dd_networks = NULL;

/* Swipe full-screen WiFi page */
static lv_obj_t *s_swipe_wifi = NULL;
static lv_obj_t *s_swipe_ssid = NULL;
static lv_obj_t *s_swipe_pass = NULL;
static lv_obj_t *s_swipe_networks = NULL;

/* Shared keyboard */
static lv_obj_t *s_kb = NULL;

static ui_wifi_save_cb_t s_wifi_cb = NULL;
static ui_wifi_scan_cb_t s_wifi_scan_cb = NULL;

static int16_t clamp16(int32_t v, int16_t min, int16_t max)
{
    if (v < min) return min;
    if (v > max) return max;
    return (int16_t)v;
}

static void style_btn_matrix(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x001a00), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00ff66), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x00ff66), 0);
}

static void hide_all_overlays(void)
{
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    if (s_swipe_wifi) lv_obj_add_flag(s_swipe_wifi, LV_OBJ_FLAG_HIDDEN);
    if (s_kb) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG_UI, "Overlays ocultos");
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
    if (s_swipe_wifi) lv_obj_add_flag(s_swipe_wifi, LV_OBJ_FLAG_HIDDEN);
    show_home_page();
    ESP_LOGI(TAG_UI, "Drawer abierto");
}

static void show_swipe_wifi_page(void)
{
    if (!s_swipe_wifi) return;
    lv_obj_clear_flag(s_swipe_wifi, LV_OBJ_FLAG_HIDDEN);
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    if (s_kb) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG_UI, "Pantalla WiFi por swipe abierta");
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
    uint8_t green = (uint8_t)(140 + (sec * 2) % 110);
    uint8_t red = (uint8_t)(10 + (sec % 20));
    lv_color_t matrix = lv_color_make(red, green, 20);
    lv_obj_set_style_text_color(s_time_lbl, matrix, 0);
    lv_obj_set_style_text_color(s_brand_lbl, lv_color_make(20, (uint8_t)(green - 40), 20), 0);

    if (s_accel_valid) {
        int16_t xoff = clamp16((int32_t)s_ax / 450, -45, 45);
        int16_t yoff = clamp16((int32_t)(-s_ay) / 450, -45, 45);
        lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, xoff, yoff + 6);
        lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, xoff / 2, 22);

        int32_t amp = ((s_ax >= 0) ? s_ax : -s_ax) + ((s_ay >= 0) ? s_ay : -s_ay);
        uint8_t glow = (uint8_t)clamp16(20 + amp / 90, 10, 100);
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_make(0, glow, 0), 0);
    } else {
        lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);
        lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 22);
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
    }
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

static void save_credentials(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG_UI, "Guardar WiFi desde UI (ssid_len=%d pass_len=%d)", (int)strlen(ssid), (int)strlen(pass));
    if (s_wifi_cb) {
        s_wifi_cb(ssid, pass);
    } else {
        ESP_LOGW(TAG_UI, "s_wifi_cb es NULL, no se aplican credenciales");
    }
}

static void btn_save_drawer_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);
    save_credentials(ssid, pass);
    hide_all_overlays();
}

static void btn_save_swipe_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_swipe_ssid);
    const char *pass = lv_textarea_get_text(s_swipe_pass);
    save_credentials(ssid, pass);
    hide_all_overlays();
}

static void btn_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "Tap cerrar overlay");
    hide_all_overlays();
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


static void dd_networks_changed_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED) return;

    char ssid[64] = {0};
    lv_dropdown_get_selected_str(dd, ssid, sizeof(ssid));
    if (ssid[0] == '\0') return;

    if (s_ta_ssid) lv_textarea_set_text(s_ta_ssid, ssid);
    if (s_swipe_ssid) lv_textarea_set_text(s_swipe_ssid, ssid);
    ESP_LOGI(TAG_UI, "SSID seleccionado desde lista: %s", ssid);
}

static void btn_scan_wifi_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_scan_cb) {
        ESP_LOGI(TAG_UI, "Solicitando escaneo WiFi...");
        s_wifi_scan_cb();
    } else {
        ESP_LOGW(TAG_UI, "No hay callback de escaneo registrado");
    }
}

static void screen_gesture_open_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    ESP_LOGI(TAG_UI, "Gesture detectado dir=%d", (int)dir);

    if (dir == LV_DIR_LEFT) {
        show_swipe_wifi_page();
    } else if (dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) {
        show_drawer();
    }
}

void ui_clock_create(void)
{
    lv_obj_t *scr = lv_screen_active();

    ESP_LOGI(TAG_UI, "ui_clock_create init");
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, screen_gesture_open_cb, LV_EVENT_GESTURE, NULL);

    s_brand_lbl = lv_label_create(scr);
    lv_label_set_text(s_brand_lbl, "wichIA");
    lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x00cc44), 0);
    lv_obj_set_style_text_letter_space(s_brand_lbl, 2, 0);
    lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 22);

    s_time_lbl = lv_label_create(scr);
    lv_label_set_text(s_time_lbl, "--:--:--");
    lv_obj_set_style_text_font(s_time_lbl, CLOCK_FONT, 0);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0x00ff66), 0);
    lv_obj_set_style_text_align(s_time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *btn_open = lv_button_create(scr);
    lv_obj_set_size(btn_open, 36, 36);
    lv_obj_align(btn_open, LV_ALIGN_RIGHT_MID, -4, 0);
    style_btn_matrix(btn_open);
    lv_obj_add_event_cb(btn_open, btn_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *open_lbl = lv_label_create(btn_open);
    lv_label_set_text(open_lbl, "W");
    lv_obj_center(open_lbl);

    lv_obj_t *btn_swipe_hint = lv_button_create(scr);
    lv_obj_set_size(btn_swipe_hint, 14, 90);
    lv_obj_align(btn_swipe_hint, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_swipe_hint, LV_OPA_40, 0);
    lv_obj_set_style_radius(btn_swipe_hint, 0, 0);
    style_btn_matrix(btn_swipe_hint);
    lv_obj_add_event_cb(btn_swipe_hint, btn_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint_lbl = lv_label_create(btn_swipe_hint);
    lv_label_set_text(hint_lbl, "≡");
    lv_obj_center(hint_lbl);

    /* Drawer menu */
    s_drawer = lv_obj_create(scr);
    lv_obj_set_size(s_drawer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_drawer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    lv_obj_set_style_border_color(s_drawer, lv_color_hex(0x00ff66), 0);

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
    style_btn_matrix(btn_close);
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
    style_btn_matrix(btn_wifi_app);
    lv_obj_add_event_cb(btn_wifi_app, btn_wifi_app_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_app_lbl = lv_label_create(btn_wifi_app);
    lv_label_set_text(wifi_app_lbl, LV_SYMBOL_WIFI "\nWiFi");
    lv_obj_center(wifi_app_lbl);

    lv_obj_t *btn_clock_app = lv_button_create(s_home_page);
    lv_obj_set_size(btn_clock_app, 92, 92);
    lv_obj_align(btn_clock_app, LV_ALIGN_TOP_RIGHT, -4, 24);
    style_btn_matrix(btn_clock_app);
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

    s_dd_networks = lv_dropdown_create(s_wifi_page);
    lv_obj_set_width(s_dd_networks, 136);
    lv_obj_align(s_dd_networks, LV_ALIGN_TOP_LEFT, 2, 24);
    lv_dropdown_set_options(s_dd_networks, "(sin escaneo)");
    lv_obj_add_event_cb(s_dd_networks, dd_networks_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_scan = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_scan, 56, 26);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -2, 24);
    style_btn_matrix(btn_scan);
    lv_obj_add_event_cb(btn_scan, btn_scan_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(btn_scan);
    lv_label_set_text(scan_lbl, "Scan");
    lv_obj_center(scan_lbl);

    lv_obj_t *btn_back = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_back, 56, 26);
    lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, -2, -2);
    style_btn_matrix(btn_back);
    lv_obj_add_event_cb(btn_back, btn_back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(btn_back);
    lv_label_set_text(back_lbl, "Apps");
    lv_obj_center(back_lbl);

    s_ta_ssid = lv_textarea_create(s_wifi_page);
    lv_obj_set_width(s_ta_ssid, 196);
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_MID, 0, 58);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_obj_add_event_cb(s_ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    s_ta_pass = lv_textarea_create(s_wifi_page);
    lv_obj_set_width(s_ta_pass, 196);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 102);
    lv_textarea_set_placeholder_text(s_ta_pass, "Password");
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_obj_add_event_cb(s_ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_save = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_save, 94, 34);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 4, -8);
    style_btn_matrix(btn_save);
    lv_obj_add_event_cb(btn_save, btn_save_drawer_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(btn_save);
    lv_label_set_text(save_lbl, "Guardar");
    lv_obj_center(save_lbl);

    lv_obj_t *btn_cancel = lv_button_create(s_wifi_page);
    lv_obj_set_size(btn_cancel, 94, 34);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -4, -8);
    style_btn_matrix(btn_cancel);
    lv_obj_add_event_cb(btn_cancel, btn_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_lbl, "Cerrar");
    lv_obj_center(cancel_lbl);

    /* Swipe full-screen WiFi page */
    s_swipe_wifi = lv_obj_create(scr);
    lv_obj_set_size(s_swipe_wifi, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_swipe_wifi, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_swipe_wifi, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_swipe_wifi, 0, 0);
    lv_obj_set_style_border_color(s_swipe_wifi, lv_color_hex(0x00ff66), 0);

    lv_obj_t *sw_title = lv_label_create(s_swipe_wifi);
    lv_label_set_text(sw_title, LV_SYMBOL_WIFI " WiFi rapido (Swipe)");
    lv_obj_align(sw_title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *sw_hint = lv_label_create(s_swipe_wifi);
    lv_label_set_text(sw_hint, "Desliza der/arriba para volver al reloj");
    lv_obj_align(sw_hint, LV_ALIGN_TOP_MID, 0, 28);

    s_swipe_networks = lv_dropdown_create(s_swipe_wifi);
    lv_obj_set_width(s_swipe_networks, 150);
    lv_obj_align(s_swipe_networks, LV_ALIGN_TOP_LEFT, 12, 54);
    lv_dropdown_set_options(s_swipe_networks, "(sin escaneo)");
    lv_obj_add_event_cb(s_swipe_networks, dd_networks_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sw_scan = lv_button_create(s_swipe_wifi);
    lv_obj_set_size(sw_scan, 62, 30);
    lv_obj_align(sw_scan, LV_ALIGN_TOP_RIGHT, -12, 54);
    style_btn_matrix(sw_scan);
    lv_obj_add_event_cb(sw_scan, btn_scan_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sw_scan_lbl = lv_label_create(sw_scan);
    lv_label_set_text(sw_scan_lbl, "Scan");
    lv_obj_center(sw_scan_lbl);

    s_swipe_ssid = lv_textarea_create(s_swipe_wifi);
    lv_obj_set_width(s_swipe_ssid, 214);
    lv_obj_align(s_swipe_ssid, LV_ALIGN_TOP_MID, 0, 95);
    lv_textarea_set_placeholder_text(s_swipe_ssid, "SSID");
    lv_obj_add_event_cb(s_swipe_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    s_swipe_pass = lv_textarea_create(s_swipe_wifi);
    lv_obj_set_width(s_swipe_pass, 214);
    lv_obj_align(s_swipe_pass, LV_ALIGN_TOP_MID, 0, 142);
    lv_textarea_set_placeholder_text(s_swipe_pass, "Password");
    lv_textarea_set_password_mode(s_swipe_pass, true);
    lv_obj_add_event_cb(s_swipe_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *sw_save = lv_button_create(s_swipe_wifi);
    lv_obj_set_size(sw_save, 102, 36);
    lv_obj_align(sw_save, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    style_btn_matrix(sw_save);
    lv_obj_add_event_cb(sw_save, btn_save_swipe_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sw_save_lbl = lv_label_create(sw_save);
    lv_label_set_text(sw_save_lbl, "Guardar");
    lv_obj_center(sw_save_lbl);

    lv_obj_t *sw_close = lv_button_create(s_swipe_wifi);
    lv_obj_set_size(sw_close, 102, 36);
    lv_obj_align(sw_close, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    style_btn_matrix(sw_close);
    lv_obj_add_event_cb(sw_close, btn_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sw_close_lbl = lv_label_create(sw_close);
    lv_label_set_text(sw_close_lbl, "Cerrar");
    lv_obj_center(sw_close_lbl);

    s_kb = lv_keyboard_create(scr);
    lv_obj_set_size(s_kb, lv_pct(100), 120);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    hide_all_overlays();
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


void ui_clock_set_wifi_scan_callback(ui_wifi_scan_cb_t cb)
{
    s_wifi_scan_cb = cb;
}

void ui_clock_set_scan_results(const char *options_newline)
{
    const char *opts = (options_newline && options_newline[0]) ? options_newline : "(sin redes)";
    if (s_dd_networks) lv_dropdown_set_options(s_dd_networks, opts);
    if (s_swipe_networks) lv_dropdown_set_options(s_swipe_networks, opts);
    ESP_LOGI(TAG_UI, "Lista de redes WiFi actualizada en UI");
}

void ui_clock_prefill_wifi(const char *ssid, const char *pass)
{
    if (s_ta_ssid && ssid) lv_textarea_set_text(s_ta_ssid, ssid);
    if (s_ta_pass && pass) lv_textarea_set_text(s_ta_pass, pass);

    if (s_swipe_ssid && ssid) lv_textarea_set_text(s_swipe_ssid, ssid);
    if (s_swipe_pass && pass) lv_textarea_set_text(s_swipe_pass, pass);
}
