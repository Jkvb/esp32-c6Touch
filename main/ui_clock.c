#include "ui_clock.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_st7789_lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"

#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
#define UI_FONT_TIME (&lv_font_montserrat_48)
#elif defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
#define UI_FONT_TIME (&lv_font_montserrat_32)
#else
#define UI_FONT_TIME LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
#define UI_FONT_TITLE (&lv_font_montserrat_32)
#else
#define UI_FONT_TITLE LV_FONT_DEFAULT
#endif

#define UI_PAGE_COUNT 3
#define UI_FACE_COUNT 3
#define UI_STATUS_COUNT 6
#define UI_DOUBLE_TAP_MS 380U

typedef enum {
    UI_PAGE_CORE = 0,
    UI_PAGE_HAND,
    UI_PAGE_SENSE,
} ui_page_t;

typedef struct {
    const char *name;
    uint32_t bg;
    uint32_t panel;
    uint32_t primary;
    uint32_t accent;
    uint32_t text;
    uint32_t muted;
} ui_theme_t;

static const char *TAG = "NERVE_UI";

static const ui_theme_t s_themes[UI_FACE_COUNT] = {
    {
        .name = "NERVE",
        .bg = 0x05070D,
        .panel = 0x0B1420,
        .primary = 0x00E5FF,
        .accent = 0xFF2E88,
        .text = 0xE7FBFF,
        .muted = 0x66828C,
    },
    {
        .name = "SYNTH",
        .bg = 0x0B0310,
        .panel = 0x17091F,
        .primary = 0xFF2E88,
        .accent = 0x7B61FF,
        .text = 0xFFF0FB,
        .muted = 0x936A89,
    },
    {
        .name = "MATRIX",
        .bg = 0x000904,
        .panel = 0x06160D,
        .primary = 0x39FF14,
        .accent = 0xF8E16C,
        .text = 0xE8FFE9,
        .muted = 0x5B8063,
    },
};

static lv_obj_t *s_tileview;
static lv_obj_t *s_pages[UI_PAGE_COUNT];
static lv_obj_t *s_header_lbl[UI_PAGE_COUNT];
static lv_obj_t *s_header_chip[UI_PAGE_COUNT];
static lv_obj_t *s_header_chip_lbl[UI_PAGE_COUNT];
static lv_obj_t *s_footer_lbl[UI_PAGE_COUNT];
static lv_obj_t *s_page_dots[UI_PAGE_COUNT][UI_PAGE_COUNT];

static lv_obj_t *s_time_row;
static lv_obj_t *s_hour_lbl;
static lv_obj_t *s_colon_lbl;
static lv_obj_t *s_minute_lbl;
static lv_obj_t *s_second_lbl;
static lv_obj_t *s_time_mode_lbl;
static lv_obj_t *s_core_state_lbl;
static lv_obj_t *s_face_hint_lbl;
static lv_obj_t *s_second_rail;
static lv_obj_t *s_scan_line;

static lv_obj_t *s_hand_card;
static lv_obj_t *s_gesture_slot_lbl;
static lv_obj_t *s_gesture_name_lbl;
static lv_obj_t *s_gesture_hint_lbl;
static lv_obj_t *s_gesture_state_lbl;
static lv_obj_t *s_finger_group;
static lv_obj_t *s_finger_bars[GESTURE_FINGER_COUNT];
static lv_obj_t *s_finger_lbls[GESTURE_FINGER_COUNT];
static lv_obj_t *s_prev_btn;
static lv_obj_t *s_prev_btn_lbl;
static lv_obj_t *s_next_btn;
static lv_obj_t *s_next_btn_lbl;
static lv_obj_t *s_apply_btn;
static lv_obj_t *s_apply_btn_lbl;

static lv_obj_t *s_status_rows[UI_STATUS_COUNT];
static lv_obj_t *s_status_keys[UI_STATUS_COUNT];
static lv_obj_t *s_status_values[UI_STATUS_COUNT];

static lv_obj_t *s_boot_overlay;
static lv_obj_t *s_boot_line;

static const gesture_profile_t *s_gestures;
static size_t s_gesture_count;
static size_t s_gesture_preview;
static size_t s_gesture_selected;
static bool s_gesture_has_selection;
static ui_gesture_request_cb_t s_gesture_cb;

static volatile int16_t s_ax;
static volatile int16_t s_ay;
static volatile bool s_imu_valid;
static volatile bool s_network_connected;
static volatile bool s_time_synced;

static uint8_t s_active_page;
static uint8_t s_theme_idx;
static uint32_t s_last_tap_ms;
static bool s_last_tap_valid;
static int32_t s_last_width;
static int32_t s_last_height;
static int s_last_second = -1;
static uint32_t s_last_status_ms;

static lv_color_t color_hex(uint32_t value)
{
    return lv_color_hex(value);
}

static const ui_theme_t *theme(void)
{
    return &s_themes[s_theme_idx % UI_FACE_COUNT];
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

static lv_obj_t *make_bare_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *obj = make_bare_container(parent);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 5, 0);
    return obj;
}

static void create_header_and_footer(ui_page_t page, const char *title, const char *chip, const char *footer)
{
    lv_obj_t *parent = s_pages[page];

    s_header_lbl[page] = make_label(parent, title, LV_FONT_DEFAULT);
    lv_obj_set_style_text_letter_space(s_header_lbl[page], 1, 0);

    s_header_chip[page] = make_panel(parent);
    s_header_chip_lbl[page] = make_label(s_header_chip[page], chip, LV_FONT_DEFAULT);
    lv_obj_center(s_header_chip_lbl[page]);

    s_footer_lbl[page] = make_label(parent, footer, LV_FONT_DEFAULT);
    lv_obj_set_style_text_letter_space(s_footer_lbl[page], 1, 0);

    for (uint8_t i = 0; i < UI_PAGE_COUNT; i++) {
        s_page_dots[page][i] = make_bare_container(parent);
        lv_obj_set_size(s_page_dots[page][i], 5, 3);
        lv_obj_set_style_bg_opa(s_page_dots[page][i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_page_dots[page][i], 2, 0);
    }
}

static void style_button(lv_obj_t *button, bool filled)
{
    const ui_theme_t *t = theme();
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_hex(t->primary), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, color_hex(filled ? t->primary : t->panel), 0);
    lv_obj_set_style_bg_color(button, color_hex(t->accent), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, color_hex(t->accent), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

static void apply_theme(void)
{
    const ui_theme_t *t = theme();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, color_hex(t->bg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_tileview, color_hex(t->bg), 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, 0);

    for (uint8_t page = 0; page < UI_PAGE_COUNT; page++) {
        lv_obj_set_style_bg_color(s_pages[page], color_hex(t->bg), 0);
        lv_obj_set_style_bg_opa(s_pages[page], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_header_lbl[page], color_hex(t->primary), 0);
        lv_obj_set_style_bg_color(s_header_chip[page], color_hex(t->panel), 0);
        lv_obj_set_style_border_color(s_header_chip[page], color_hex(t->primary), 0);
        lv_obj_set_style_text_color(s_header_chip_lbl[page], color_hex(t->primary), 0);
        lv_obj_set_style_text_color(s_footer_lbl[page], color_hex(t->muted), 0);

        for (uint8_t dot = 0; dot < UI_PAGE_COUNT; dot++) {
            uint32_t dot_color = dot == page ? t->primary : t->muted;
            lv_obj_set_style_bg_color(s_page_dots[page][dot], color_hex(dot_color), 0);
            lv_obj_set_style_bg_opa(s_page_dots[page][dot], dot == page ? LV_OPA_COVER : (lv_opa_t)90, 0);
        }
    }

    lv_label_set_text(s_header_chip_lbl[UI_PAGE_CORE], t->name);
    lv_obj_set_style_text_color(s_hour_lbl, color_hex(t->text), 0);
    lv_obj_set_style_text_color(s_colon_lbl, color_hex(t->primary), 0);
    lv_obj_set_style_text_color(s_minute_lbl, color_hex(t->text), 0);
    lv_obj_set_style_text_color(s_second_lbl, color_hex(t->accent), 0);
    lv_obj_set_style_text_color(s_time_mode_lbl, color_hex(t->muted), 0);
    lv_obj_set_style_text_color(s_core_state_lbl, color_hex(t->primary), 0);
    lv_obj_set_style_text_color(s_face_hint_lbl, color_hex(t->muted), 0);
    lv_obj_set_style_bg_color(s_second_rail, color_hex(t->panel), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_second_rail, color_hex(t->primary), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_scan_line, color_hex(t->primary), 0);

    lv_obj_set_style_bg_color(s_hand_card, color_hex(t->panel), 0);
    lv_obj_set_style_border_color(s_hand_card, color_hex(t->primary), 0);
    lv_obj_set_style_text_color(s_gesture_slot_lbl, color_hex(t->accent), 0);
    lv_obj_set_style_text_color(s_gesture_name_lbl, color_hex(t->text), 0);
    lv_obj_set_style_text_color(s_gesture_hint_lbl, color_hex(t->muted), 0);
    lv_obj_set_style_text_color(s_gesture_state_lbl, color_hex(t->primary), 0);
    for (uint8_t i = 0; i < GESTURE_FINGER_COUNT; i++) {
        lv_obj_set_style_bg_color(s_finger_bars[i], color_hex(t->muted), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_finger_bars[i], (lv_opa_t)65, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_finger_bars[i], color_hex(t->primary), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(s_finger_lbls[i], color_hex(t->muted), 0);
    }

    style_button(s_prev_btn, false);
    style_button(s_next_btn, false);
    style_button(s_apply_btn, true);
    lv_obj_set_style_text_color(s_prev_btn_lbl, color_hex(t->primary), 0);
    lv_obj_set_style_text_color(s_next_btn_lbl, color_hex(t->primary), 0);
    lv_obj_set_style_text_color(s_apply_btn_lbl, color_hex(t->bg), 0);

    for (uint8_t i = 0; i < UI_STATUS_COUNT; i++) {
        lv_obj_set_style_bg_color(s_status_rows[i], color_hex(t->panel), 0);
        lv_obj_set_style_border_color(s_status_rows[i], color_hex(t->muted), 0);
        lv_obj_set_style_text_color(s_status_keys[i], color_hex(t->muted), 0);
        lv_obj_set_style_text_color(s_status_values[i], color_hex(t->primary), 0);
    }

    lv_obj_invalidate(screen);
}

static uint8_t page_index_from_obj(lv_obj_t *obj)
{
    for (uint8_t i = 0; i < UI_PAGE_COUNT; i++) {
        if (obj == s_pages[i]) return i;
    }
    return UI_PAGE_CORE;
}

static void tile_changed_cb(lv_event_t *event)
{
    lv_obj_t *tileview = lv_event_get_target(event);
    lv_obj_t *active = lv_tileview_get_tile_active(tileview);
    s_active_page = page_index_from_obj(active);
    ESP_LOGI(TAG, "Pagina activa=%u", (unsigned)(s_active_page + 1U));
}

static void core_tap_cb(lv_event_t *event)
{
    (void)event;
    uint32_t now = lv_tick_get();

    if (s_last_tap_valid && lv_tick_elaps(s_last_tap_ms) <= UI_DOUBLE_TAP_MS) {
        s_last_tap_valid = false;
        s_theme_idx = (uint8_t)((s_theme_idx + 1U) % UI_FACE_COUNT);
        apply_theme();
        ESP_LOGI(TAG, "Watchface=%s", theme()->name);
    } else {
        s_last_tap_ms = now;
        s_last_tap_valid = true;
    }
}

static void refresh_gesture(bool animate)
{
    if (!s_gestures || s_gesture_count == 0) return;

    const gesture_profile_t *profile = &s_gestures[s_gesture_preview];
    lv_label_set_text(s_gesture_slot_lbl, profile->code);
    lv_label_set_text(s_gesture_name_lbl, profile->name);
    lv_label_set_text(s_gesture_hint_lbl, profile->hint);

    for (uint8_t i = 0; i < GESTURE_FINGER_COUNT; i++) {
        lv_bar_set_value(s_finger_bars[i], profile->finger[i], animate ? LV_ANIM_ON : LV_ANIM_OFF);
    }

    bool selected = s_gesture_has_selection && s_gesture_selected == s_gesture_preview;
    lv_label_set_text(s_apply_btn_lbl, selected ? "PRESET SELECTED" : "SELECT PRESET");
    lv_label_set_text(s_gesture_state_lbl,
                      selected ? "PREVIEW SAVED // MOTOR OFF" : "SWIPE UP/DOWN // PREVIEW");
}

static void change_gesture(int delta)
{
    if (s_gesture_count == 0) return;
    int next = (int)s_gesture_preview + delta;
    if (next < 0) next = (int)s_gesture_count - 1;
    if (next >= (int)s_gesture_count) next = 0;
    s_gesture_preview = (size_t)next;
    refresh_gesture(true);
}

static void gesture_arrow_cb(lv_event_t *event)
{
    intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    change_gesture(delta < 0 ? -1 : 1);
}

static void gesture_apply_cb(lv_event_t *event)
{
    (void)event;
    if (!s_gestures || s_gesture_count == 0) return;

    s_gesture_selected = s_gesture_preview;
    s_gesture_has_selection = true;
    refresh_gesture(false);

    const gesture_profile_t *profile = &s_gestures[s_gesture_selected];
    ESP_LOGI(TAG, "Preset seleccionado: %s (%s), preview sin motores", profile->name, profile->code);
    if (s_gesture_cb) s_gesture_cb(profile);
}

static void gesture_swipe_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (direction == LV_DIR_TOP) {
        change_gesture(1);
        lv_event_stop_bubbling(event);
    } else if (direction == LV_DIR_BOTTOM) {
        change_gesture(-1);
        lv_event_stop_bubbling(event);
    }
}

static void create_core_page(void)
{
    lv_obj_t *page = s_pages[UI_PAGE_CORE];
    create_header_and_footer(UI_PAGE_CORE, "IAWICHU // CORE", "NERVE", "01 // CORE");

    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, core_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);

    s_time_row = make_bare_container(page);
    lv_obj_set_flex_flow(s_time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_hour_lbl = make_label(s_time_row, "00", UI_FONT_TIME);
    s_colon_lbl = make_label(s_time_row, ":", UI_FONT_TIME);
    s_minute_lbl = make_label(s_time_row, "00", UI_FONT_TIME);
    s_second_lbl = make_label(page, "00 SEC", LV_FONT_DEFAULT);
    lv_obj_set_style_text_letter_space(s_second_lbl, 2, 0);

    s_time_mode_lbl = make_label(page, "UPTIME // T+000:00:00", LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_time_mode_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_core_state_lbl = make_label(page, "TOUCH -- // IMU --", LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_core_state_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_face_hint_lbl = make_label(page, "DOUBLE TAP // CHANGE FACE", LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_face_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_second_rail = lv_bar_create(page);
    lv_bar_set_range(s_second_rail, 0, 59);
    lv_bar_set_value(s_second_rail, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_second_rail, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_second_rail, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_second_rail, (lv_opa_t)55, LV_PART_MAIN);

    s_scan_line = make_bare_container(page);
    lv_obj_set_style_bg_opa(s_scan_line, (lv_opa_t)28, 0);
}

static void create_hand_page(void)
{
    static const char *finger_names[GESTURE_FINGER_COUNT] = {"T", "I", "M", "R", "L"};
    lv_obj_t *page = s_pages[UI_PAGE_HAND];
    create_header_and_footer(UI_PAGE_HAND, "HAND // GESTURES", "PREVIEW", "02 // HAND");

    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, gesture_swipe_cb, LV_EVENT_GESTURE, NULL);

    s_hand_card = make_panel(page);
    lv_obj_add_flag(s_hand_card, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_gesture_slot_lbl = make_label(page, "G-00", LV_FONT_DEFAULT);
    lv_obj_set_style_text_letter_space(s_gesture_slot_lbl, 2, 0);
    s_gesture_name_lbl = make_label(page, "OPEN", UI_FONT_TITLE);
    s_gesture_hint_lbl = make_label(page, "Neutral hand / safe start", LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_gesture_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_gesture_hint_lbl, LV_LABEL_LONG_WRAP);
    s_gesture_state_lbl = make_label(page, "SWIPE UP/DOWN // PREVIEW", LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_gesture_state_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_finger_group = make_bare_container(page);
    for (uint8_t i = 0; i < GESTURE_FINGER_COUNT; i++) {
        s_finger_bars[i] = lv_bar_create(s_finger_group);
        lv_obj_set_size(s_finger_bars[i], 12, 52);
        lv_obj_set_pos(s_finger_bars[i], (int32_t)i * 23, 0);
        lv_bar_set_range(s_finger_bars[i], 0, 100);
        lv_obj_set_style_radius(s_finger_bars[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_finger_bars[i], 1, LV_PART_INDICATOR);
        lv_obj_set_style_anim_duration(s_finger_bars[i], 180, 0);

        s_finger_lbls[i] = make_label(s_finger_group, finger_names[i], LV_FONT_DEFAULT);
        lv_obj_set_pos(s_finger_lbls[i], (int32_t)i * 23 + 2, 56);
    }

    s_prev_btn = lv_button_create(page);
    lv_obj_add_flag(s_prev_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_prev_btn_lbl = make_label(s_prev_btn, "<", LV_FONT_DEFAULT);
    lv_obj_center(s_prev_btn_lbl);
    lv_obj_add_event_cb(s_prev_btn, gesture_arrow_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)-1);

    s_next_btn = lv_button_create(page);
    lv_obj_add_flag(s_next_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_next_btn_lbl = make_label(s_next_btn, ">", LV_FONT_DEFAULT);
    lv_obj_center(s_next_btn_lbl);
    lv_obj_add_event_cb(s_next_btn, gesture_arrow_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)1);

    s_apply_btn = lv_button_create(page);
    lv_obj_add_flag(s_apply_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_apply_btn_lbl = make_label(s_apply_btn, "SELECT PRESET", LV_FONT_DEFAULT);
    lv_obj_center(s_apply_btn_lbl);
    lv_obj_add_event_cb(s_apply_btn, gesture_apply_cb, LV_EVENT_SHORT_CLICKED, NULL);

    s_gestures = gesture_profiles_get(&s_gesture_count);
    refresh_gesture(false);
}

static void create_sense_page(void)
{
    static const char *keys[UI_STATUS_COUNT] = {
        "TOUCH", "IMU", "CLOCK", "NETWORK", "ROTATION", "HEAP"
    };
    lv_obj_t *page = s_pages[UI_PAGE_SENSE];
    create_header_and_footer(UI_PAGE_SENSE, "SENSE // STATUS", "LIVE", "03 // SENSE");

    for (uint8_t i = 0; i < UI_STATUS_COUNT; i++) {
        s_status_rows[i] = make_panel(page);
        s_status_keys[i] = make_label(s_status_rows[i], keys[i], LV_FONT_DEFAULT);
        s_status_values[i] = make_label(s_status_rows[i], "--", LV_FONT_DEFAULT);
        lv_obj_align(s_status_keys[i], LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_align(s_status_values[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void layout_common(bool portrait)
{
    for (uint8_t page = 0; page < UI_PAGE_COUNT; page++) {
        lv_obj_align(s_header_lbl[page], LV_ALIGN_TOP_LEFT, portrait ? 10 : 8, portrait ? 10 : 4);
        lv_obj_set_size(s_header_chip[page], portrait ? 57 : 64, portrait ? 22 : 20);
        lv_obj_align(s_header_chip[page], LV_ALIGN_TOP_RIGHT, portrait ? -8 : -6, portrait ? 7 : 2);
        lv_obj_align(s_footer_lbl[page], LV_ALIGN_BOTTOM_LEFT, 8, portrait ? -6 : -2);
        for (uint8_t dot = 0; dot < UI_PAGE_COUNT; dot++) {
            lv_obj_align(s_page_dots[page][dot], LV_ALIGN_BOTTOM_RIGHT,
                         -8 - (int32_t)(UI_PAGE_COUNT - 1U - dot) * 10,
                         portrait ? -9 : -5);
        }
    }
}

static void layout_core(bool portrait, int32_t width, int32_t height)
{
    (void)height;
    if (portrait) {
        lv_obj_set_size(s_time_row, 158, 58);
        lv_obj_align(s_time_row, LV_ALIGN_CENTER, 0, -30);
        lv_obj_align(s_second_lbl, LV_ALIGN_CENTER, 0, 11);
        lv_obj_set_width(s_time_mode_lbl, 154);
        lv_obj_set_style_text_align(s_time_mode_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_time_mode_lbl, LV_ALIGN_CENTER, 0, 40);
        lv_obj_set_width(s_core_state_lbl, 154);
        lv_obj_set_style_text_align(s_core_state_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_core_state_lbl, LV_ALIGN_BOTTOM_MID, 0, -48);
        lv_obj_set_width(s_face_hint_lbl, 154);
        lv_obj_set_style_text_align(s_face_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_face_hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -29);
        lv_obj_set_size(s_second_rail, 3, 174);
        lv_obj_align(s_second_rail, LV_ALIGN_LEFT_MID, 8, 1);
    } else {
        lv_obj_set_size(s_time_row, 160, 56);
        lv_obj_align(s_time_row, LV_ALIGN_LEFT_MID, 25, -7);
        lv_obj_align(s_second_lbl, LV_ALIGN_LEFT_MID, 92, 28);
        lv_obj_set_width(s_time_mode_lbl, 122);
        lv_obj_set_style_text_align(s_time_mode_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_time_mode_lbl, LV_ALIGN_RIGHT_MID, -12, -20);
        lv_obj_set_width(s_core_state_lbl, 122);
        lv_obj_set_style_text_align(s_core_state_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_core_state_lbl, LV_ALIGN_RIGHT_MID, -12, 4);
        lv_obj_set_width(s_face_hint_lbl, 122);
        lv_obj_set_style_text_align(s_face_hint_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_face_hint_lbl, LV_ALIGN_RIGHT_MID, -12, 29);
        lv_obj_set_size(s_second_rail, 3, 96);
        lv_obj_align(s_second_rail, LV_ALIGN_LEFT_MID, 9, 1);
    }
    lv_obj_set_size(s_scan_line, width - 20, 1);
}

static void layout_hand(bool portrait)
{
    if (portrait) {
        lv_obj_set_size(s_hand_card, 150, 184);
        lv_obj_align(s_hand_card, LV_ALIGN_CENTER, 0, -3);
        lv_obj_align(s_gesture_slot_lbl, LV_ALIGN_TOP_MID, 0, 49);
        lv_obj_align(s_gesture_name_lbl, LV_ALIGN_TOP_MID, 0, 68);
        lv_obj_set_width(s_gesture_hint_lbl, 142);
        lv_obj_set_style_text_align(s_gesture_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_gesture_hint_lbl, LV_ALIGN_TOP_MID, 0, 106);
        lv_obj_set_size(s_finger_group, 105, 74);
        lv_obj_align(s_finger_group, LV_ALIGN_TOP_MID, 0, 140);
        lv_obj_set_size(s_prev_btn, 34, 38);
        lv_obj_align(s_prev_btn, LV_ALIGN_LEFT_MID, 10, 17);
        lv_obj_set_size(s_next_btn, 34, 38);
        lv_obj_align(s_next_btn, LV_ALIGN_RIGHT_MID, -10, 17);
        lv_obj_set_width(s_gesture_state_lbl, 154);
        lv_obj_set_style_text_align(s_gesture_state_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_gesture_state_lbl, LV_ALIGN_BOTTOM_MID, 0, -72);
        lv_obj_set_size(s_apply_btn, 138, 36);
        lv_obj_align(s_apply_btn, LV_ALIGN_BOTTOM_MID, 0, -33);
    } else {
        lv_obj_set_size(s_hand_card, 304, 112);
        lv_obj_align(s_hand_card, LV_ALIGN_CENTER, 0, 2);
        lv_obj_align(s_gesture_slot_lbl, LV_ALIGN_TOP_LEFT, 12, 34);
        lv_obj_align(s_gesture_name_lbl, LV_ALIGN_TOP_LEFT, 12, 48);
        lv_obj_set_width(s_gesture_hint_lbl, 125);
        lv_obj_set_style_text_align(s_gesture_hint_lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(s_gesture_hint_lbl, LV_ALIGN_TOP_LEFT, 12, 84);
        lv_obj_set_size(s_finger_group, 105, 74);
        lv_obj_align(s_finger_group, LV_ALIGN_TOP_LEFT, 169, 40);
        lv_obj_set_size(s_prev_btn, 30, 34);
        lv_obj_align(s_prev_btn, LV_ALIGN_TOP_LEFT, 135, 57);
        lv_obj_set_size(s_next_btn, 30, 34);
        lv_obj_align(s_next_btn, LV_ALIGN_TOP_RIGHT, -8, 57);
        lv_obj_set_width(s_gesture_state_lbl, 132);
        lv_obj_set_style_text_align(s_gesture_state_lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(s_gesture_state_lbl, LV_ALIGN_BOTTOM_LEFT, 12, -28);
        lv_obj_set_size(s_apply_btn, 134, 32);
        lv_obj_align(s_apply_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -22);
    }
}

static void layout_sense(bool portrait)
{
    for (uint8_t i = 0; i < UI_STATUS_COUNT; i++) {
        if (portrait) {
            lv_obj_set_size(s_status_rows[i], 154, 31);
            lv_obj_set_pos(s_status_rows[i], 8, 46 + (int32_t)i * 36);
        } else {
            int32_t col = i / 3;
            int32_t row = i % 3;
            lv_obj_set_size(s_status_rows[i], 148, 31);
            lv_obj_set_pos(s_status_rows[i], 8 + col * 156, 31 + row * 37);
        }
        lv_obj_align(s_status_keys[i], LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_align(s_status_values[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void apply_layout(void)
{
    lv_display_t *display = lv_display_get_default();
    if (!display) return;

    int32_t width = lv_display_get_horizontal_resolution(display);
    int32_t height = lv_display_get_vertical_resolution(display);
    bool portrait = height >= width;
    s_last_width = width;
    s_last_height = height;

    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));
    layout_common(portrait);
    layout_core(portrait, width, height);
    layout_hand(portrait);
    layout_sense(portrait);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
}

static void set_status_value(uint8_t index, const char *value, bool good)
{
    if (index >= UI_STATUS_COUNT) return;
    lv_label_set_text(s_status_values[index], value);
    lv_obj_set_style_text_color(s_status_values[index],
                                color_hex(good ? theme()->primary : theme()->accent), 0);
}

static void update_status(void)
{
    bool touch_ok = display_st7789_touch_ready();
    bool imu_ok = s_imu_valid;
    bool net_ok = s_network_connected;
    bool clock_ok = s_time_synced;
    char buffer[24];

    set_status_value(0, touch_ok ? "ONLINE" : "OFFLINE", touch_ok);
    set_status_value(1, imu_ok ? "ONLINE" : "OFFLINE", imu_ok);
    set_status_value(2, clock_ok ? "NTP SYNC" : "UPTIME", true);
    set_status_value(3, net_ok ? "CONNECTED" : "OFFLINE", net_ok);

    static const char *rot_names[] = {"0 DEG", "90 DEG", "180 DEG", "270 DEG"};
    disp_rot_t rotation = display_st7789_get_rotation();
    set_status_value(4, rot_names[(uint8_t)rotation & 0x03U], true);

    snprintf(buffer, sizeof(buffer), "%" PRIu32 " KB", esp_get_free_heap_size() / 1024U);
    set_status_value(5, buffer, true);

    snprintf(buffer, sizeof(buffer), "TOUCH %s // IMU %s",
             touch_ok ? "OK" : "--", imu_ok ? "OK" : "--");
    lv_label_set_text(s_core_state_lbl, buffer);
}

static void update_clock(void)
{
    time_t now = 0;
    struct tm time_info = {0};
    time(&now);
    localtime_r(&now, &time_info);

    uint64_t uptime_s = (uint64_t)esp_timer_get_time() / 1000000ULL;
    bool wall_clock_valid = time_info.tm_year >= (2024 - 1900);
    int second = wall_clock_valid ? time_info.tm_sec : (int)(uptime_s % 60ULL);
    if (second == s_last_second) return;
    s_last_second = second;

    char hour[4];
    char minute[4];
    char seconds[12];
    char mode[64];

    if (wall_clock_valid) {
        snprintf(hour, sizeof(hour), "%02d", time_info.tm_hour);
        snprintf(minute, sizeof(minute), "%02d", time_info.tm_min);
        snprintf(seconds, sizeof(seconds), "%02d SEC", time_info.tm_sec);
        snprintf(mode, sizeof(mode), "%02d.%02d.%04d // %s",
                 time_info.tm_mday, time_info.tm_mon + 1, time_info.tm_year + 1900,
                 s_time_synced ? "NTP" : "LOCAL");
    } else {
        uint64_t hours = (uptime_s / 3600ULL) % 100ULL;
        uint64_t minutes = (uptime_s / 60ULL) % 60ULL;
        snprintf(hour, sizeof(hour), "%02" PRIu64, hours);
        snprintf(minute, sizeof(minute), "%02" PRIu64, minutes);
        snprintf(seconds, sizeof(seconds), "%02" PRIu64 " SEC", (uint64_t)(uptime_s % 60ULL));
        snprintf(mode, sizeof(mode), "UPTIME // T+%03" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                 (uint64_t)(uptime_s / 3600ULL), minutes, (uint64_t)(uptime_s % 60ULL));
    }

    lv_label_set_text(s_hour_lbl, hour);
    lv_label_set_text(s_minute_lbl, minute);
    lv_label_set_text(s_second_lbl, seconds);
    lv_label_set_text(s_time_mode_lbl, mode);
    lv_bar_set_value(s_second_rail, second, LV_ANIM_OFF);
}

static void update_scan_line(void)
{
    int32_t top = s_last_height >= s_last_width ? 42 : 27;
    int32_t bottom = s_last_height >= s_last_width ? 280 : 145;
    int32_t span = bottom - top;
    if (span < 1) span = 1;
    int32_t y = top + (int32_t)((lv_tick_get() / 24U) % (uint32_t)span);
    lv_obj_set_pos(s_scan_line, 10, y);

    bool bright = ((lv_tick_get() / 500U) & 1U) == 0U;
    lv_obj_set_style_text_opa(s_colon_lbl, bright ? LV_OPA_COVER : (lv_opa_t)75, 0);
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    bool rotation_changed = display_st7789_service();
    lv_display_t *display = lv_display_get_default();
    int32_t width = display ? lv_display_get_horizontal_resolution(display) : 0;
    int32_t height = display ? lv_display_get_vertical_resolution(display) : 0;
    if (rotation_changed || width != s_last_width || height != s_last_height) {
        lv_tileview_set_tile_by_index(s_tileview, s_active_page, 0, LV_ANIM_OFF);
        apply_layout();
        ESP_LOGI(TAG, "Layout=%" PRIi32 "x%" PRIi32 " rot=%d", width, height,
                 (int)display_st7789_get_rotation());
    }

    update_clock();
    update_scan_line();

    uint32_t now_ms = lv_tick_get();
    if (lv_tick_elaps(s_last_status_ms) >= 500U) {
        s_last_status_ms = now_ms;
        update_status();
    }

    if (s_last_tap_valid && lv_tick_elaps(s_last_tap_ms) > UI_DOUBLE_TAP_MS) {
        s_last_tap_valid = false;
    }
}

static void boot_line_anim_cb(void *object, int32_t value)
{
    lv_obj_set_width((lv_obj_t *)object, value);
}

static void boot_done_cb(lv_timer_t *timer)
{
    if (s_boot_overlay) {
        lv_obj_delete_async(s_boot_overlay);
        s_boot_overlay = NULL;
        s_boot_line = NULL;
    }
    lv_timer_delete(timer);
}

static void create_boot_overlay(void)
{
    const ui_theme_t *t = theme();
    lv_obj_t *screen = lv_screen_active();

    s_boot_overlay = make_bare_container(screen);
    lv_obj_set_size(s_boot_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_boot_overlay, color_hex(t->bg), 0);
    lv_obj_set_style_bg_opa(s_boot_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_boot_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_boot_overlay);

    lv_obj_t *brand = make_label(s_boot_overlay, "IAWICHU", UI_FONT_TITLE);
    lv_obj_set_style_text_color(brand, color_hex(t->text), 0);
    lv_obj_set_style_text_letter_space(brand, 3, 0);
    lv_obj_align(brand, LV_ALIGN_CENTER, 0, -25);

    lv_obj_t *subtitle = make_label(s_boot_overlay, "NERVE OS // INITIALIZING", LV_FONT_DEFAULT);
    lv_obj_set_style_text_color(subtitle, color_hex(t->primary), 0);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 17);

    s_boot_line = make_bare_container(s_boot_overlay);
    lv_obj_set_size(s_boot_line, 0, 2);
    lv_obj_set_style_bg_color(s_boot_line, color_hex(t->accent), 0);
    lv_obj_set_style_bg_opa(s_boot_line, LV_OPA_COVER, 0);
    lv_obj_align(s_boot_line, LV_ALIGN_CENTER, 0, 42);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_boot_line);
    lv_anim_set_exec_cb(&anim, boot_line_anim_cb);
    lv_anim_set_values(&anim, 0, 126);
    lv_anim_set_duration(&anim, 520);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    lv_timer_create(boot_done_cb, 950, NULL);
}

void ui_clock_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_tileview = lv_tileview_create(screen);
    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_set_style_pad_all(s_tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_tileview, LV_DIR_HOR);
    lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(s_tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    for (uint8_t i = 0; i < UI_PAGE_COUNT; i++) {
        s_pages[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
        lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_pages[i], 0, 0);
        lv_obj_set_style_border_width(s_pages[i], 0, 0);
    }

    create_core_page();
    create_hand_page();
    create_sense_page();
    apply_theme();
    apply_layout();
    update_clock();
    update_status();
    lv_tileview_set_tile_by_index(s_tileview, UI_PAGE_CORE, 0, LV_ANIM_OFF);

    lv_timer_create(ui_timer_cb, 100, NULL);
    create_boot_overlay();

    ESP_LOGI(TAG, "NERVE OS listo: swipe CORE/HAND/SENSE, doble tap watchface");
}

void ui_clock_set_touch_debug(int16_t x, int16_t y, bool pressed)
{
    (void)x;
    (void)y;
    (void)pressed;
}

void ui_clock_set_accel(int16_t x, int16_t y, bool valid)
{
    s_ax = x;
    s_ay = y;
    s_imu_valid = valid;
}

void ui_clock_set_network_state(bool connected, bool time_synced)
{
    s_network_connected = connected;
    s_time_synced = time_synced;
}

void ui_clock_set_gesture_request_callback(ui_gesture_request_cb_t cb)
{
    s_gesture_cb = cb;
}
