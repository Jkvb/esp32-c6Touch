#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t accel_qmi8658_init(void);
esp_err_t accel_qmi8658_read_xyz(int16_t *x, int16_t *y, int16_t *z);
bool accel_qmi8658_is_ready(void);
