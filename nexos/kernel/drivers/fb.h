/* NexOS — kernel/drivers/fb.h | Linear framebuffer driver | MIT License */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t *addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
    uint8_t   initialized;
} framebuffer_t;

extern framebuffer_t fb;

/* ── Theme-switchable colour palette (Catppuccin Mocha defaults) ── */
extern uint32_t col_base;
extern uint32_t col_mantle;
extern uint32_t col_crust;
extern uint32_t col_surface0;
extern uint32_t col_surface1;
extern uint32_t col_surface2;
extern uint32_t col_overlay0;
extern uint32_t col_text;
extern uint32_t col_subtext;
extern uint32_t col_blue;
extern uint32_t col_lavender;
extern uint32_t col_mauve;
extern uint32_t col_red;
extern uint32_t col_peach;
extern uint32_t col_yellow;
extern uint32_t col_green;
extern uint32_t col_teal;
extern uint32_t col_sky;

#define COL_BASE     col_base
#define COL_MANTLE   col_mantle
#define COL_CRUST    col_crust
#define COL_SURFACE0 col_surface0
#define COL_SURFACE1 col_surface1
#define COL_SURFACE2 col_surface2
#define COL_OVERLAY0 col_overlay0
#define COL_TEXT     col_text
#define COL_SUBTEXT  col_subtext
#define COL_BLUE     col_blue
#define COL_LAVENDER col_lavender
#define COL_MAUVE    col_mauve
#define COL_RED      col_red
#define COL_PEACH    col_peach
#define COL_YELLOW   col_yellow
#define COL_GREEN    col_green
#define COL_TEAL     col_teal
#define COL_SKY      col_sky

void     fb_init(uint64_t addr, uint32_t w, uint32_t h,
                 uint32_t pitch, uint8_t bpp);
void     fb_put_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);
void     fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void     fb_draw_rect_outline(int x, int y, int w, int h,
                              uint32_t color, int thickness);
void     fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void     fb_fill_rounded_rect(int x, int y, int w, int h,
                              int radius, uint32_t color);
void     fb_draw_circle(int cx, int cy, int r, uint32_t color);
void     fb_fill_circle(int cx, int cy, int r, uint32_t color);
void     fb_clear(uint32_t color);
void     fb_scroll_up(int pixels, uint32_t bg_color);
uint32_t fb_blend(uint32_t fg, uint32_t bg, uint8_t alpha);
void     fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h);
