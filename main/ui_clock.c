#include "ui_clock.h"
#include "lvgl.h"
#include <time.h>
#include <stdio.h>

#if defined(LV_FONT_MONTSERRAT_48) && (LV_FONT_MONTSERRAT_48)
  #define CLOCK_FONT (&lv_font_montserrat_48)
#elif defined(LV_FONT_MONTSERRAT_32) && (LV_FONT_MONTSERRAT_32)
  #define CLOCK_FONT (&lv_font_montserrat_32)
#else
  #define CLOCK_FONT (LV_FONT_DEFAULT)
#endif

static lv_obj_t *s_lbl = NULL;

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    char buf[32];
    if (ti.tm_year < (2024 - 1900)) {
        snprintf(buf, sizeof(buf), "OK\n--:--:--");
    } else {
        snprintf(buf, sizeof(buf), "OK\n%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    lv_label_set_text(s_lbl, buf);
}

void ui_clock_create(void)
{
    lv_obj_t *scr = lv_screen_active();

    // Fondo azul brillante (si esto se ve, el panel está vivo)
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0047FF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_lbl = lv_label_create(scr);
    lv_label_set_text(s_lbl, "OK\n--:--:--");

    lv_obj_set_style_text_font(s_lbl, CLOCK_FONT, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_center(s_lbl);

    lv_timer_create(clock_timer_cb, 250, NULL);
}