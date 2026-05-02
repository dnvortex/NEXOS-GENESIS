/* NexOS — kernel/gui/notif.h | Toast notification system | MIT License */
#pragma once
#include <stdint.h>

#define NOTIF_MAX 4

void notif_init(void);
void notif_show(const char *title, const char *body, uint32_t ms);
void notif_draw(void);
void notif_tick(uint32_t delta_ms);
