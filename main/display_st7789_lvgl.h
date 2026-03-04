#pragma once
#include "lvgl.h"

typedef enum {
    DISP_ROT_0 = 0,
    DISP_ROT_90,
    DISP_ROT_180,
    DISP_ROT_270,
} disp_rot_t;

lv_display_t* display_st7789_lvgl_init(void);
void display_st7789_set_rotation(disp_rot_t rot);
disp_rot_t display_st7789_get_rotation(void);