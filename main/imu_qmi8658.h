#pragma once
#include "esp_err.h"

typedef struct { float ax, ay, az; } imu_accel_t;

esp_err_t imu_qmi8658_init(void);
esp_err_t imu_qmi8658_read_accel(imu_accel_t *out);