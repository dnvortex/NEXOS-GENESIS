/* NexOS — kernel/drivers/font.h | 8x16 bitmap font renderer | MIT License */
#pragma once
#include <stdint.h>

void font_putchar(int x, int y, char c, uint32_t fg, uint32_t bg);
void font_puts(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void font_printf(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...);
void font_putchar2x(int x, int y, char c, uint32_t fg, uint32_t bg);
void font_puts2x(int x, int y, const char *s, uint32_t fg, uint32_t bg);
int  font_str_width(const char *s);
int  font_str_width2x(const char *s);
