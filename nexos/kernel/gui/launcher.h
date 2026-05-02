/* NexOS — kernel/gui/launcher.h | App launcher menu | MIT License */
#pragma once

void launcher_show(int x, int y);
void launcher_hide(void);
void launcher_draw(void);
void launcher_handle_click(int x, int y);
void launcher_handle_mouse(int x, int y);
void launcher_handle_key(char key);
int  launcher_is_visible(void);
