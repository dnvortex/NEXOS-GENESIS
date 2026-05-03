/* NexOS — kernel/gui/snake_app.h | Snake Game | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

#define SN_W        30
#define SN_H        24
#define SN_CELL     16
#define SN_HEADER_H 36
#define SN_MAX_LEN  (SN_W * SN_H)

typedef struct {
    window_t *win;
    uint8_t   bx[SN_MAX_LEN];
    uint8_t   by[SN_MAX_LEN];
    int       head;
    int       body_len;
    int       dx, dy;
    int       next_dx, next_dy;
    int       food_x, food_y;
    int       score;
    int       best;
    int       game_over;
    int       paused;
    uint64_t  last_move;
    uint32_t  speed_ms;
    uint8_t   grid[SN_H][SN_W];
    uint32_t  rng;
} snake_app_t;

snake_app_t *snake_create(int x, int y);
