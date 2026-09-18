#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gesture_profiles.h"

typedef void (*ui_gesture_request_cb_t)(const gesture_profile_t *profile);

void ui_clock_create(void);
void ui_clock_set_touch_debug(int16_t x, int16_t y, bool pressed);
void ui_clock_set_accel(int16_t x, int16_t y, bool valid);
void ui_clock_set_network_state(bool connected, bool time_synced);
void ui_clock_set_gesture_request_callback(ui_gesture_request_cb_t cb);
