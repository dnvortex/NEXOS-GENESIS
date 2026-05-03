/* NexOS — kernel/gui/desktop.h | Desktop background renderer | MIT License */
#pragma once
#include <stdint.h>

void desktop_draw(void);
void desktop_set_phase(uint32_t delta_ms);

/* Repaint a rectangular region of the desktop background (gradient + dots).
   Call this when a window's old position is exposed (e.g. during a drag)
   instead of triggering a full-screen desktop_draw(). */
void desktop_paint_rect(int x, int y, int w, int h);
