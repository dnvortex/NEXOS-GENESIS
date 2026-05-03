/* NexOS — kernel/gui/taskbar.h | Bottom taskbar | MIT License */
#pragma once
#include <stdint.h>

#define TB_H 40

void taskbar_init(void);
void taskbar_draw(void);
void taskbar_handle_click(int x, int y);
void taskbar_handle_mouse(int mx, int my);
void taskbar_update(void);
int  taskbar_get_y(void);
