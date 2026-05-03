/* NexOS — kernel/gui/sysmon_app.h | System Monitor | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

#define SYSMON_HISTORY 60

typedef struct {
    window_t  *win;
    uint8_t    heap_hist[SYSMON_HISTORY];   /* heap usage % history */
    uint8_t    pmm_hist[SYSMON_HISTORY];    /* physical mem usage % history */
    int        hist_pos;
    uint64_t   last_update;
    int        proc_scroll;
} sysmon_app_t;

sysmon_app_t *sysmon_create(int x, int y);
