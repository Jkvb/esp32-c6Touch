#pragma once

#include <stdbool.h>

#include "lvgl.h"

typedef enum {
    DISP_ROT_0 = 0,
    DISP_ROT_90,
    DISP_ROT_180,
    DISP_ROT_270,
} disp_rot_t;

lv_display_t* display_st7789_lvgl_init(void);
bool display_st7789_set_rotation(disp_rot_t rot);
void display_st7789_request_rotation(disp_rot_t rot);
bool display_st7789_service(void);
disp_rot_t display_st7789_get_rotation(void);
bool display_st7789_touch_ready(void);
