#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*ui_wifi_save_cb_t)(const char *ssid, const char *pass);

void ui_clock_create(void);
void ui_clock_set_accel(int16_t x, int16_t y, bool valid);
void ui_clock_set_wifi_callback(ui_wifi_save_cb_t cb);
void ui_clock_prefill_wifi(const char *ssid, const char *pass);
