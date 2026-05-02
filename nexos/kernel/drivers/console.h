/* NexOS — kernel/drivers/console.h | Framebuffer text console | MIT License */
#pragma once
#include <stdint.h>

void console_init(void);
void console_putchar(char c);
void console_puts(const char *s);
void console_printf(const char *fmt, ...);
void console_set_color(uint32_t fg, uint32_t bg);
void console_clear(void);
void console_set_pos(int x, int y);
void console_get_pos(int *x, int *y);
