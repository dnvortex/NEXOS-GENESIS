/* NexOS — kernel/gui/calc_app.h | GUI Calculator | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

typedef struct {
    window_t *win;
    char      display[24];
    int64_t   operand;
    char      pending_op;
    int       has_operand;
    int       new_number;
    int       error;
} calc_app_t;

calc_app_t *calc_create(int x, int y);
