#pragma once

#include <stddef.h>
#include <stdint.h>

#define GESTURE_FINGER_COUNT 5

typedef struct {
    uint8_t id;
    const char *code;
    const char *name;
    const char *hint;
    uint8_t finger[GESTURE_FINGER_COUNT];
    uint8_t speed;
} gesture_profile_t;

const gesture_profile_t *gesture_profiles_get(size_t *count);
