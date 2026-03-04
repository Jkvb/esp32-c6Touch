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

static uint8_t s_active_tile = 0;
static uint8_t s_last_rot_quadrant = 0;
static uint8_t s_rot_candidate = 0;
static uint8_t s_rot_stable_count = 0;
static lv_point_t s_press_start = {0, 0};
static bool s_swipe_consumed = false;

static const int16_t SWIPE_NAV_DELTA_PX = 20;
static const uint8_t SWIPE_DELTA_WINDOW = 3;

static int16_t s_swipe_dx_hist[3] = {0};
static uint8_t s_swipe_dx_idx = 0;
static uint8_t s_swipe_dx_count = 0;
static lv_point_t s_prev_point = {0, 0};


#define UI_TOUCH_DEBUG_OVERLAY 1
#define UI_TOUCH_DBG_W 170
#define UI_TOUCH_DBG_H 320

static lv_obj_t *s_touch_cross_h = NULL;
static lv_obj_t *s_touch_cross_v = NULL;
static lv_obj_t *s_touch_x_lbl = NULL;
static lv_obj_t *s_touch_y_lbl = NULL;
static int16_t s_touch_dbg_last_x = -1;
static int16_t s_touch_dbg_last_y = -1;
static uint32_t s_touch_dbg_update_count = 0;

static ui_wifi_save_cb_t s_wifi_cb = NULL;
static ui_wifi_scan_cb_t s_wifi_scan_cb = NULL;

static uint8_t tile_index_from_obj(lv_obj_t *obj)
{
    for (uint8_t i = 0; i < 5; i++) {
        if (obj == s_tiles[i]) return i;
    }
    return 0;
}

static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    s_active_tile = tile_index_from_obj(act);
    ESP_LOGI(TAG_UI, "Pantalla activa=%d (touch swipe)", s_active_tile + 1);
}



static int16_t swipe_dx_window_push_and_sum(int16_t dx_step)
{
    s_swipe_dx_hist[s_swipe_dx_idx] = dx_step;
    s_swipe_dx_idx = (uint8_t)((s_swipe_dx_idx + 1U) % SWIPE_DELTA_WINDOW);
    if (s_swipe_dx_count < SWIPE_DELTA_WINDOW) {
        s_swipe_dx_count++;
    }

    int32_t sum = 0;
    for (uint8_t i = 0; i < s_swipe_dx_count; i++) {
        sum += s_swipe_dx_hist[i];
    }
    return (int16_t)sum;
}

static void swipe_dx_window_reset(void)
{
    memset(s_swipe_dx_hist, 0, sizeof(s_swipe_dx_hist));
    s_swipe_dx_idx = 0;
    s_swipe_dx_count = 0;
}

static void go_to_tile(uint8_t next, const char *reason)
{
    if (next > 4) next = 4;
    if (next == s_active_tile) return;
    lv_tileview_set_tile_by_index(s_tileview, next, 0, LV_ANIM_ON);
    ESP_LOGI(TAG_UI, "Pantalla activa=%d (%s)", next + 1, reason);
}

static void tile_press_cb(lv_event_t *e)
{
    (void)e;
    s_swipe_consumed = false;

    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &s_press_start);
    } else {
        s_press_start.x = 0;
        s_press_start.y = 0;
    }

    s_prev_point = s_press_start;
    swipe_dx_window_reset();
}

static void tile_pressing_cb(lv_event_t *e)
{
    (void)e;

    if (s_swipe_consumed) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t cur = {0};
    lv_indev_get_point(indev, &cur);

    int16_t dx = (int16_t)(cur.x - s_press_start.x);
    int16_t dy = (int16_t)(cur.y - s_press_start.y);
    int16_t adx = dx >= 0 ? dx : -dx;
    int16_t ady = dy >= 0 ? dy : -dy;
    int16_t dx_step = (int16_t)(cur.x - s_prev_point.x);
    s_prev_point = cur;

    int16_t dx_sum3 = swipe_dx_window_push_and_sum(dx_step);
    int16_t adx_sum3 = dx_sum3 >= 0 ? dx_sum3 : -dx_sum3;

    if (adx_sum3 < SWIPE_NAV_DELTA_PX) {
        return;
    }

    if (dx_sum3 < 0 && s_active_tile < 4) {
        go_to_tile((uint8_t)(s_active_tile + 1), "swipe drag sum3");
        ESP_LOGI(TAG_UI, "Swipe drag aceptado -> sig pantalla dx=%d dy=%d sum3=%d |adx|=%d |ady|=%d", (int)dx, (int)dy, (int)dx_sum3, (int)adx, (int)ady);
        s_swipe_consumed = true;
    } else if (dx_sum3 > 0 && s_active_tile > 0) {
        go_to_tile((uint8_t)(s_active_tile - 1), "swipe drag sum3");
        ESP_LOGI(TAG_UI, "Swipe drag aceptado -> pantalla ant dx=%d dy=%d sum3=%d |adx|=%d |ady|=%d", (int)dx, (int)dy, (int)dx_sum3, (int)adx, (int)ady);
        s_swipe_consumed = true;
    }
}

static void tile_release_cb(lv_event_t *e)
{
    (void)e;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t cur = {0};
    lv_indev_get_point(indev, &cur);

    int16_t dx = (int16_t)(cur.x - s_press_start.x);
    int16_t dy = (int16_t)(cur.y - s_press_start.y);
    int16_t adx = dx >= 0 ? dx : -dx;
    int16_t ady = dy >= 0 ? dy : -dy;

    ESP_LOGI(TAG_UI, "Swipe release dx=%d dy=%d |adx|=%d |ady|=%d consumed=%d", (int)dx, (int)dy, (int)adx, (int)ady, (int)s_swipe_consumed);

    int16_t dx_sum3 = 0;
    for (uint8_t i = 0; i < s_swipe_dx_count; i++) {
        dx_sum3 = (int16_t)(dx_sum3 + s_swipe_dx_hist[i]);
    }
    int16_t adx_sum3 = dx_sum3 >= 0 ? dx_sum3 : -dx_sum3;

    bool by_sum3 = (adx_sum3 >= SWIPE_NAV_DELTA_PX);
    bool by_total = (adx >= SWIPE_NAV_DELTA_PX);
    if (s_swipe_consumed || (!by_sum3 && !by_total)) {
        s_swipe_consumed = false;
        swipe_dx_window_reset();
        return;
    }

    int16_t dir_dx = by_sum3 ? dx_sum3 : dx;
    if (dir_dx < 0 && s_active_tile < 4) {
        go_to_tile((uint8_t)(s_active_tile + 1), by_sum3 ? "swipe release sum3" : "swipe release total");
        ESP_LOGI(TAG_UI, "Swipe release aceptado -> sig pantalla dx=%d dy=%d sum3=%d total=%d", (int)dx, (int)dy, (int)dx_sum3, (int)dx);
    } else if (dir_dx > 0 && s_active_tile > 0) {
        go_to_tile((uint8_t)(s_active_tile - 1), by_sum3 ? "swipe release sum3" : "swipe release total");
        ESP_LOGI(TAG_UI, "Swipe release aceptado -> pantalla ant dx=%d dy=%d sum3=%d total=%d", (int)dx, (int)dy, (int)dx_sum3, (int)dx);
    }

    s_swipe_consumed = false;
    swipe_dx_window_reset();
}

static void tile_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT && s_active_tile < 4) {
        go_to_tile((uint8_t)(s_active_tile + 1), "gesture");
        s_swipe_consumed = true;
    } else if (dir == LV_DIR_RIGHT && s_active_tile > 0) {
        go_to_tile((uint8_t)(s_active_tile - 1), "gesture");
        s_swipe_consumed = true;
    }
}

static uint8_t accel_to_quadrant(int16_t ax, int16_t ay, bool valid)
{
    if (!valid) return s_last_rot_quadrant;

    const int16_t TH = 4200; /* ~0.25g con 16384 LSB/g */
    int32_t abs_x = ax >= 0 ? ax : -ax;
    int32_t abs_y = ay >= 0 ? ay : -ay;

    if (abs_x < TH && abs_y < TH) {
        return s_last_rot_quadrant;
    }

    if (abs_y >= abs_x) {
        return (ay >= 0) ? 2 : 0;
    }
    return (ax >= 0) ? 3 : 1;
}

static int16_t quadrant_to_deg10(uint8_t q)
{
    switch (q) {
        case 1: return 900;
        case 2: return 1800;
        case 3: return 2700;
        default: return 0;
    }
}

static void update_clock_rotation_from_accel(void)
{
    uint8_t q = accel_to_quadrant(s_ax, s_ay, s_accel_valid);

    if (q == s_rot_candidate) {
        if (s_rot_stable_count < 255) s_rot_stable_count++;
    } else {
        s_rot_candidate = q;
        s_rot_stable_count = 0;
    }

    if (s_rot_stable_count < 3 || q == s_last_rot_quadrant) {
        return;
    }

    s_last_rot_quadrant = q;
    int16_t deg10 = quadrant_to_deg10(q);

    lv_obj_set_style_transform_pivot_x(s_time_lbl, lv_obj_get_width(s_time_lbl) / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_time_lbl, 22, 0);
    lv_obj_set_style_transform_rotation(s_time_lbl, deg10, 0);

    lv_obj_set_style_transform_pivot_x(s_brand_lbl, lv_obj_get_width(s_brand_lbl) / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_brand_lbl, 10, 0);
    lv_obj_set_style_transform_rotation(s_brand_lbl, deg10, 0);

    ESP_LOGI(TAG_UI, "Rotación reloj=%d° (ax=%d ay=%d)", (int)(deg10 / 10), (int)s_ax, (int)s_ay);
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

    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);
    lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 18);

    update_clock_rotation_from_accel();
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
    ESP_LOGI(TAG_UI, "Swipe cfg nav_delta_px=%d over last %u dx steps", (int)SWIPE_NAV_DELTA_PX, (unsigned)SWIPE_DELTA_WINDOW);

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_tileview, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_add_event_cb(s_tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tileview, tile_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_tileview, tile_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_tileview, tile_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_tileview, tile_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_set_scroll_snap_x(s_tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);

    for (uint8_t i = 0; i < 5; i++) {
        s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
        lv_obj_add_flag(s_tiles[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(s_tiles[i], tile_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_tiles[i], tile_pressing_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_tiles[i], tile_release_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_tiles[i], tile_gesture_cb, LV_EVENT_GESTURE, NULL);
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
    lv_obj_set_width(s_time_lbl, lv_pct(98));
    lv_obj_set_style_pad_left(s_time_lbl, 2, 0);
    lv_obj_set_style_pad_right(s_time_lbl, 2, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *hint = lv_label_create(s_tiles[0]);
    lv_label_set_text(hint, "Desliza para ver pantallas 2-5");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x007722), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    create_info_tile(s_tiles[1], "Pantalla 2", "Estado sensores", "(maqueta touch)");
    create_info_tile(s_tiles[2], "Pantalla 3", "Estado WiFi", "(maqueta touch)");
    create_info_tile(s_tiles[3], "Pantalla 4", "Estado reloj", "(maqueta touch)");
    create_info_tile(s_tiles[4], "Pantalla 5", "Debug", "(maqueta touch)");

    lv_tileview_set_tile_by_index(s_tileview, 0, 0, LV_ANIM_OFF);

#if UI_TOUCH_DEBUG_OVERLAY
    s_touch_cross_h = lv_obj_create(scr);
    lv_obj_remove_style_all(s_touch_cross_h);
    lv_obj_set_size(s_touch_cross_h, 17, 2);
    lv_obj_set_style_bg_color(s_touch_cross_h, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(s_touch_cross_h, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_touch_cross_h, LV_OBJ_FLAG_HIDDEN);

    s_touch_cross_v = lv_obj_create(scr);
    lv_obj_remove_style_all(s_touch_cross_v);
    lv_obj_set_size(s_touch_cross_v, 2, 17);
    lv_obj_set_style_bg_color(s_touch_cross_v, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(s_touch_cross_v, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_touch_cross_v, LV_OBJ_FLAG_HIDDEN);

    s_touch_x_lbl = lv_label_create(scr);
    lv_label_set_text(s_touch_x_lbl, "X:0");
    lv_obj_set_style_text_color(s_touch_x_lbl, lv_color_hex(0xFF0000), 0);
    lv_obj_align(s_touch_x_lbl, LV_ALIGN_TOP_LEFT, 3, 3);

    s_touch_y_lbl = lv_label_create(scr);
    lv_label_set_text(s_touch_y_lbl, "Y:0");
    lv_obj_set_style_text_color(s_touch_y_lbl, lv_color_hex(0x00FF00), 0);
    lv_obj_align(s_touch_y_lbl, LV_ALIGN_TOP_LEFT, 52, 3);
#endif

    lv_timer_create(clock_timer_cb, 200, NULL);
}


void ui_clock_set_touch_debug(int16_t x, int16_t y, bool pressed)
{
#if UI_TOUCH_DEBUG_OVERLAY
    if (!s_touch_cross_h || !s_touch_cross_v || !s_touch_x_lbl || !s_touch_y_lbl) {
        return;
    }

    if (!pressed) {
        lv_obj_add_flag(s_touch_cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_touch_cross_v, LV_OBJ_FLAG_HIDDEN);
        s_touch_dbg_last_x = -1;
        s_touch_dbg_last_y = -1;
        return;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (UI_TOUCH_DBG_W - 1)) x = (UI_TOUCH_DBG_W - 1);
    if (y > (UI_TOUCH_DBG_H - 1)) y = (UI_TOUCH_DBG_H - 1);

    lv_obj_remove_flag(s_touch_cross_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_touch_cross_v, LV_OBJ_FLAG_HIDDEN);

    int16_t cx = x < 8 ? 0 : (int16_t)(x - 8);
    int16_t cy = y < 8 ? 0 : (int16_t)(y - 8);
    if (x != s_touch_dbg_last_x || y != s_touch_dbg_last_y) {
        lv_obj_set_pos(s_touch_cross_h, cx, y);
        lv_obj_set_pos(s_touch_cross_v, x, cy);
        s_touch_dbg_last_x = x;
        s_touch_dbg_last_y = y;
    }

    s_touch_dbg_update_count++;
    if ((s_touch_dbg_update_count % 4U) == 0U) {
        lv_label_set_text_fmt(s_touch_x_lbl, "X:%d", (int)x);
        lv_label_set_text_fmt(s_touch_y_lbl, "Y:%d", (int)y);
    }
#endif
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
