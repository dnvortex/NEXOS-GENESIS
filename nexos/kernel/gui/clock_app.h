/* NexOS — kernel/gui/clock_app.h | GUI Clock | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

typedef struct {
    window_t *win;
    uint8_t   last_sec;
} clock_app_t;

clock_app_t *clock_create(int x, int y);
