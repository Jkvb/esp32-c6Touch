#include "gesture_profiles.h"

/*
 * Finger order: thumb, index, middle, ring, little.
 * Values describe the UI preview only: 0=open, 100=closed.
 * A future motor supervisor must validate and clamp every command before
 * moving the prosthesis.
 */
static const gesture_profile_t s_profiles[] = {
    {
        .id = 0,
        .code = "G-00",
        .name = "OPEN",
        .hint = "Neutral hand / safe start",
        .finger = {8, 5, 5, 5, 5},
        .speed = 45,
    },
    {
        .id = 1,
        .code = "G-01",
        .name = "POWER",
        .hint = "Full cylindrical grip",
        .finger = {82, 94, 96, 96, 92},
        .speed = 62,
    },
    {
        .id = 2,
        .code = "G-02",
        .name = "PINCH",
        .hint = "Thumb + index precision",
        .finger = {78, 82, 18, 14, 12},
        .speed = 40,
    },
    {
        .id = 3,
        .code = "G-03",
        .name = "POINT",
        .hint = "Index extended",
        .finger = {72, 4, 91, 94, 92},
        .speed = 55,
    },
    {
        .id = 4,
        .code = "G-04",
        .name = "TRIPOD",
        .hint = "Three-finger precision",
        .finger = {76, 79, 76, 18, 14},
        .speed = 42,
    },
    {
        .id = 5,
        .code = "G-05",
        .name = "PEACE",
        .hint = "Index + middle extended",
        .finger = {74, 5, 5, 93, 94},
        .speed = 50,
    },
};

const gesture_profile_t *gesture_profiles_get(size_t *count)
{
    if (count) {
        *count = sizeof(s_profiles) / sizeof(s_profiles[0]);
    }
    return s_profiles;
}
