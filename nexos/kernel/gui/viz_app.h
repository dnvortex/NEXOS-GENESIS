/* NexOS — kernel/gui/viz_app.h | Music Visualizer | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

#define VIZ_BARS 36

typedef struct {
    window_t *win;
    uint32_t  frame;
    int       bar_h[VIZ_BARS];     /* current height (0-255 scaled to pixels) */
    int       peak[VIZ_BARS];      /* peak hold value */
    int       peak_hold[VIZ_BARS]; /* frames before peak drops */
    uint8_t   wave[640];           /* waveform sample buffer */
    int       wave_pos;
} viz_app_t;

viz_app_t *viz_create(int x, int y);
