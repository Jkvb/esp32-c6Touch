#include "ui_clock.h"
#include "display_st7789_lvgl.h"
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

#define UI_COLOR_BG        0x000000
#define UI_COLOR_BG_DARK   0x000000
#define UI_COLOR_TEXT_MAIN 0x39FF14
#define UI_COLOR_TEXT_SUB  0x1ED760

static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tiles[6] = {0};
static lv_obj_t *s_brand_lbl = NULL;
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_date_lbl = NULL;
static char s_time_text[16] = "--:--:--";
static char s_date_text[24] = "--/--/----";

static volatile int16_t s_ax = 0;
static volatile int16_t s_ay = 0;
static volatile bool s_accel_valid = false;

static uint8_t s_active_tile = 0;
static uint8_t s_last_rot_quadrant = 0;
static uint8_t s_rot_candidate = 0;
static uint8_t s_rot_stable_count = 0;
static disp_rot_t s_last_disp_rot_seen = DISP_ROT_0;
static uint8_t s_watchface_idx = 0;
static bool s_watchface_select_mode = false;
static bool s_face_pressing = false;
static bool s_face_longpress_done = false;
static uint32_t s_face_press_ms = 0;
static uint32_t s_last_click_ms = 0;
#define UI_TOUCH_DEBUG_OVERLAY 0
#define UI_TOUCH_LOG_ENABLE 0
#define UI_TOUCH_DBG_W 170
#define UI_TOUCH_DBG_H 320
#define UI_ROT_STABLE_SAMPLES 2
#define UI_WATCHFACE_COUNT 5
#define UI_FACE_HOLD_MS 3000
#define UI_FACE_DBLCLICK_MS 350

#if UI_TOUCH_DEBUG_OVERLAY
static lv_obj_t *s_touch_cross_h = NULL;
static lv_obj_t *s_touch_cross_v = NULL;
static lv_obj_t *s_touch_x_lbl = NULL;
static lv_obj_t *s_touch_y_lbl = NULL;
static int16_t s_touch_dbg_last_x = -1;
static int16_t s_touch_dbg_last_y = -1;
static uint32_t s_touch_dbg_update_count = 0;
#endif
static uint32_t s_touch_diag_move_samples = 0;
static bool s_touch_diag_prev_pressed = false;
static int16_t s_touch_diag_last_x = -32768;
static int16_t s_touch_diag_last_y = -32768;


static ui_wifi_save_cb_t s_wifi_cb = NULL;
static ui_wifi_scan_cb_t s_wifi_scan_cb = NULL;


static uint8_t tile_index_from_obj(lv_obj_t *obj)
{
    for (uint8_t i = 0; i < 6; i++) {
        if (obj == s_tiles[i]) return i;
    }
    return 0;
}

static void set_active_tile(uint8_t idx, lv_anim_enable_t anim)
{
    if (!s_tileview) return;
    if (idx > 5) idx = 5;
    s_active_tile = idx;
    lv_tileview_set_tile_by_index(s_tileview, idx, 0, anim);
}

static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    s_active_tile = tile_index_from_obj(act);
    if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "Pantalla activa=%d (touch swipe)", s_active_tile + 1);
}

static const char *touch_dir_to_str(lv_dir_t dir)
{
    switch (dir) {
        case LV_DIR_LEFT: return "LEFT";
        case LV_DIR_RIGHT: return "RIGHT";
        case LV_DIR_TOP: return "TOP";
        case LV_DIR_BOTTOM: return "BOTTOM";
        default: return "NONE";
    }
}

static void tileview_touch_diag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {.x = -1, .y = -1};
    if (indev) {
        lv_indev_get_point(indev, &p);
    }

    if (code == LV_EVENT_PRESSED) {
        s_touch_diag_prev_pressed = true;
        s_touch_diag_last_x = p.x;
        s_touch_diag_last_y = p.y;
        if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH PRESSED x=%d y=%d", (int)p.x, (int)p.y);
    } else if (code == LV_EVENT_PRESSING) {
        if (p.x != s_touch_diag_last_x || p.y != s_touch_diag_last_y) {
            s_touch_diag_last_x = p.x;
            s_touch_diag_last_y = p.y;
            s_touch_diag_move_samples++;
            if ((s_touch_diag_move_samples % 8U) == 0U) {
                if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH MOVE x=%d y=%d", (int)p.x, (int)p.y);
            }
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (s_touch_diag_prev_pressed) {
            if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH RELEASED x=%d y=%d", (int)p.x, (int)p.y);
        }
        s_touch_diag_prev_pressed = false;
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
        if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH GESTURE dir=%s x=%d y=%d", touch_dir_to_str(dir), (int)p.x, (int)p.y);
    }
}



static lv_obj_t *s_cb_status_lbl[5] = {0};

static void checkbox_changed_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    uintptr_t idx_u = (uintptr_t)lv_event_get_user_data(e);
    uint8_t idx = (uint8_t)idx_u;
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);

    if (idx < 5 && s_cb_status_lbl[idx]) {
        lv_label_set_text_fmt(s_cb_status_lbl[idx], "Estado: %s", checked ? "ACTIVO" : "INACTIVO");
    }
}

static void style_checkbox_variant(lv_obj_t *cb, uint8_t style_id)
{
    /* Base negro + verde para todas las variantes */
    lv_obj_set_style_text_color(cb, lv_color_hex(UI_COLOR_TEXT_MAIN), 0);
    lv_obj_set_style_bg_color(cb, lv_color_hex(UI_COLOR_BG_DARK), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(cb, lv_color_hex(UI_COLOR_TEXT_SUB), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(cb, 2, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(cb, 0, LV_PART_INDICATOR);

    switch (style_id) {
        case 0:
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x00AA44), LV_PART_INDICATOR | LV_STATE_CHECKED);
            break;
        case 1:
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x008833), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_border_color(cb, lv_color_hex(0x22CC66), LV_PART_INDICATOR);
            break;
        case 2:
            lv_obj_set_style_radius(cb, 12, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x00CC55), LV_PART_INDICATOR | LV_STATE_CHECKED);
            break;
        case 3:
            lv_obj_set_style_shadow_width(cb, 0, LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x00AA55), LV_PART_INDICATOR | LV_STATE_CHECKED);
            break;
        case 4:
        default:
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x00DD66), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_border_color(cb, lv_color_hex(0x66EE99), LV_PART_INDICATOR);
            break;
    }
}

static void create_checkbox_tile(lv_obj_t *tile, uint8_t idx)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);

    lv_obj_t *ttl = lv_label_create(tile);
    lv_label_set_text_fmt(ttl, "Pantalla %d", (int)(idx + 1));
    lv_obj_set_style_text_color(ttl, lv_color_hex(UI_COLOR_TEXT_MAIN), 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *cb = lv_checkbox_create(tile);
    char cb_txt[32];
    snprintf(cb_txt, sizeof(cb_txt), "Estilo checkbox %d", (int)(idx + 1));
    lv_checkbox_set_text(cb, cb_txt);
    style_checkbox_variant(cb, idx);
    lv_obj_align(cb, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_text_color(cb, lv_color_hex(UI_COLOR_TEXT_MAIN), 0);
    lv_obj_add_event_cb(cb, checkbox_changed_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)idx);

    s_cb_status_lbl[idx] = lv_label_create(tile);
    lv_label_set_text(s_cb_status_lbl[idx], "Estado: INACTIVO");
    lv_obj_set_style_text_color(s_cb_status_lbl[idx], lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_align(s_cb_status_lbl[idx], LV_ALIGN_CENTER, 0, 26);
}

static void apply_watchface(uint8_t idx)
{
    if (!s_tiles[0] || !s_brand_lbl || !s_time_lbl || !s_date_lbl) return;

    switch (idx % UI_WATCHFACE_COUNT) {
        case 0:
            lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0x39FF14), 0);
            lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x1ED760), 0);
            lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x1ED760), 0);
            lv_obj_set_style_text_letter_space(s_brand_lbl, 2, 0);
            break;
        case 1:
            lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x00110A), 0);
            lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0x66FF66), 0);
            lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x33DD88), 0);
            lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x33DD88), 0);
            lv_obj_set_style_text_letter_space(s_brand_lbl, 3, 0);
            break;
        case 2:
            lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x050505), 0);
            lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0x00FFAA), 0);
            lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x00CC88), 0);
            lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x00CC88), 0);
            lv_obj_set_style_text_letter_space(s_brand_lbl, 1, 0);
            break;
        case 3:
            lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0xB7FF00), 0);
            lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x7FD100), 0);
            lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x7FD100), 0);
            lv_obj_set_style_text_letter_space(s_brand_lbl, 4, 0);
            break;
        case 4:
        default:
            lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(0x000A00), 0);
            lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0x44FF99), 0);
            lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x22DD77), 0);
            lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(0x22DD77), 0);
            lv_obj_set_style_text_letter_space(s_brand_lbl, 2, 0);
            break;
    }
}

static void clock_face_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        s_face_pressing = true;
        s_face_longpress_done = false;
        s_face_press_ms = lv_tick_get();
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (s_face_pressing && !s_face_longpress_done && lv_tick_elaps(s_face_press_ms) >= UI_FACE_HOLD_MS) {
            s_face_longpress_done = true;
            s_watchface_select_mode = true;
            ESP_LOGI(TAG_UI, "Modo watchface ON (doble click para cambiar)");
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        s_face_pressing = false;
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        uint32_t now = lv_tick_get();
        bool is_double = (now - s_last_click_ms) <= UI_FACE_DBLCLICK_MS;
        s_last_click_ms = now;

        if (is_double && s_watchface_select_mode) {
            s_watchface_idx = (uint8_t)((s_watchface_idx + 1U) % UI_WATCHFACE_COUNT);
            apply_watchface(s_watchface_idx);
            ESP_LOGI(TAG_UI, "Watchface seleccionado=%d", (int)(s_watchface_idx + 1));
        }
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

    if (s_rot_stable_count < UI_ROT_STABLE_SAMPLES || q == s_last_rot_quadrant) {
        return;
    }

    s_last_rot_quadrant = q;

    /*
     * Rotación visual de LVGL por transform en labels puede causar creación
     * de draw buffers/layers en software y disparar WDT en runtimes largos.
     * La rotación final se aplica ahora a nivel display desde imu_task.
     */
    ESP_LOGI(TAG_UI, "Rot detectada para display=%d° (ax=%d ay=%d)",
             (int)(quadrant_to_deg10(q) / 10), (int)s_ax, (int)s_ay);
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;

    static int s_last_sec = -1;

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    if (ti.tm_sec != s_last_sec) {
        s_last_sec = ti.tm_sec;

        if (ti.tm_year < (2024 - 1900)) {
            snprintf(s_time_text, sizeof(s_time_text), "--:--:--");
            snprintf(s_date_text, sizeof(s_date_text), "--/--/----");
        } else {
            snprintf(s_time_text, sizeof(s_time_text), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
            strftime(s_date_text, sizeof(s_date_text), "%d/%m/%Y", &ti);
        }
        lv_label_set_text_static(s_time_lbl, s_time_text);
        if (s_date_lbl) lv_label_set_text_static(s_date_lbl, s_date_text);
    }

    disp_rot_t cur_rot = display_st7789_get_rotation();
    if (cur_rot != s_last_disp_rot_seen) {
        s_last_disp_rot_seen = cur_rot;
        /* Re-centrar tile activo para evitar vista 50/50 tras rotación. */
        set_active_tile(s_active_tile, LV_ANIM_OFF);
        lv_obj_update_layout(s_tileview);
        ESP_LOGI(TAG_UI, "TileView realineado tras rotación=%d (tile=%d)", (int)cur_rot, (int)s_active_tile + 1);
    }

    /* Rotación por acelerómetro (hora + fecha + marca, respuesta rápida) */
    update_clock_rotation_from_accel();
}

void ui_clock_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    ESP_LOGI(TAG_UI, "ui_clock_create init (modo 6 pantallas)");
    ESP_LOGI(TAG_UI, "Swipe lateral habilitado (pantallas fijas)");

    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG_DARK), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_tileview, lv_color_hex(UI_COLOR_BG_DARK), 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_add_event_cb(s_tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_scroll_snap_x(s_tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_snap_y(s_tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(s_tileview, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_tileview, tileview_touch_diag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_tileview, tileview_touch_diag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_tileview, tileview_touch_diag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_tileview, tileview_touch_diag_cb, LV_EVENT_GESTURE, NULL);

    ESP_LOGI(TAG_UI, "TileView fijo con 6 pantallas (1..6)");

    for (uint8_t i = 0; i < 6; i++) {
        s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
        lv_obj_clear_flag(s_tiles[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_tiles[i], lv_pct(100), lv_pct(100));
    }

    /* Pantalla 1: Reloj (fondo negro, letras verdes) */
    lv_obj_set_style_bg_color(s_tiles[0], lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_tiles[0], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tiles[0], 0, 0);
    lv_obj_add_flag(s_tiles[0], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_tiles[0], clock_face_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_tiles[0], clock_face_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_tiles[0], clock_face_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_tiles[0], clock_face_touch_cb, LV_EVENT_CLICKED, NULL);

    s_brand_lbl = lv_label_create(s_tiles[0]);
    lv_label_set_text(s_brand_lbl, "wichIA");
    lv_obj_set_style_text_letter_space(s_brand_lbl, 2, 0);
    lv_obj_set_style_text_color(s_brand_lbl, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_align(s_brand_lbl, LV_ALIGN_TOP_MID, 0, 18);

    s_time_lbl = lv_label_create(s_tiles[0]);
    lv_label_set_text_static(s_time_lbl, s_time_text);
    lv_obj_set_style_text_font(s_time_lbl, CLOCK_FONT, 0);
    lv_obj_set_style_text_align(s_time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(UI_COLOR_TEXT_MAIN), 0);
    lv_obj_set_width(s_time_lbl, lv_pct(98));
    lv_obj_set_style_pad_left(s_time_lbl, 2, 0);
    lv_obj_set_style_pad_right(s_time_lbl, 2, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, -19);

    s_date_lbl = lv_label_create(s_tiles[0]);
    lv_label_set_text_static(s_date_lbl, s_date_text);
    lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_letter_space(s_date_lbl, 1, 0);
    lv_obj_set_width(s_date_lbl, lv_pct(98));
    lv_obj_set_style_text_align(s_date_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_date_lbl, LV_ALIGN_CENTER, 0, 46);
    apply_watchface(s_watchface_idx);

    for (uint8_t i = 1; i < 6; i++) {
        create_checkbox_tile(s_tiles[i], (uint8_t)(i - 1));
    }

    set_active_tile(0, LV_ANIM_OFF);
    s_last_disp_rot_seen = display_st7789_get_rotation();
    lv_timer_create(clock_timer_cb, 100, NULL);

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

}


void ui_clock_set_touch_debug(int16_t x, int16_t y, bool pressed)
{
    if (pressed) {
        if (x != s_touch_diag_last_x || y != s_touch_diag_last_y) {
            s_touch_diag_last_x = x;
            s_touch_diag_last_y = y;
            s_touch_diag_move_samples++;
            if ((s_touch_diag_move_samples % 10U) == 0U) {
                if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH RAW x=%d y=%d pressed=1", (int)x, (int)y);
            }
        }
    } else if (s_touch_diag_prev_pressed) {
        if (UI_TOUCH_LOG_ENABLE) ESP_LOGI(TAG_UI, "TOUCH RAW release");
    }
    s_touch_diag_prev_pressed = pressed;
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
    ESP_LOGI(TAG_UI, "WiFi UI deshabilitada en modo reloj+5 checkboxes");
}

void ui_clock_prefill_wifi(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    ESP_LOGI(TAG_UI, "WiFi UI deshabilitada en modo reloj+5 checkboxes");
}
