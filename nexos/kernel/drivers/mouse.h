/* NexOS — kernel/drivers/mouse.h | PS/2 mouse driver | MIT License */
#pragma once
#include <stdint.h>

void    mouse_init(void);
int     mouse_get_x(void);
int     mouse_get_y(void);
uint8_t mouse_get_btns(void);
int     mouse_left(void);
int     mouse_right(void);
int     mouse_needs_update(void);
void    cursor_restore(void);
void    cursor_draw(int x, int y);
