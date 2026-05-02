/* NexOS — kernel/drivers/fb.c | Linear framebuffer driver | MIT License */
#include "fb.h"
#include <stdint.h>

framebuffer_t fb = {0};

/* ── Catppuccin Mocha defaults ─────────────────────────────────────────── */
uint32_t col_base     = 0x1E1E2E;
uint32_t col_mantle   = 0x181825;
uint32_t col_crust    = 0x11111B;
uint32_t col_surface0 = 0x313244;
uint32_t col_surface1 = 0x45475A;
uint32_t col_surface2 = 0x585B70;
uint32_t col_overlay0 = 0x6C7086;
uint32_t col_text     = 0xCDD6F4;
uint32_t col_subtext  = 0xA6ADC8;
uint32_t col_blue     = 0x89B4FA;
uint32_t col_lavender = 0xB4BEFE;
uint32_t col_mauve    = 0xCBA6F7;
uint32_t col_red      = 0xF38BA8;
uint32_t col_peach    = 0xFAB387;
uint32_t col_yellow   = 0xF9E2AF;
uint32_t col_green    = 0xA6E3A1;
uint32_t col_teal     = 0x94E2D5;
uint32_t col_sky      = 0x89DCEB;

void fb_init(uint64_t addr, uint32_t w, uint32_t h,
             uint32_t pitch, uint8_t bpp) {
    fb.addr        = (uint32_t *)(uintptr_t)addr;
    fb.width       = w;
    fb.height      = h;
    fb.pitch       = pitch;
    fb.bpp         = bpp;
    fb.initialized = 1;
}

void fb_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height)
        return;
    uint32_t *row = (uint32_t *)((uint8_t *)fb.addr + (uint32_t)y * fb.pitch);
    row[x] = color;
}

uint32_t fb_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height)
        return 0;
    uint32_t *row = (uint32_t *)((uint8_t *)fb.addr + (uint32_t)y * fb.pitch);
    return row[x];
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)fb.width)  x1 = (int)fb.width;
    if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int row = y; row < y1; row++) {
        uint32_t *p = (uint32_t *)((uint8_t *)fb.addr + (uint32_t)row * fb.pitch) + x;
        for (int col = x; col < x1; col++)
            *p++ = c;
    }
}

void fb_draw_rect_outline(int x, int y, int w, int h,
                          uint32_t color, int t) {
    fb_fill_rect(x,         y,         w, t, color);
    fb_fill_rect(x,         y + h - t, w, t, color);
    fb_fill_rect(x,         y,         t, h, color);
    fb_fill_rect(x + w - t, y,         t, h, color);
}

void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void fb_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (r <= 0 || w <= 0 || h <= 0) { fb_fill_rect(x, y, w, h, c); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    /* centre strips */
    fb_fill_rect(x + r, y,         w - 2 * r, h, c);
    fb_fill_rect(x,     y + r,     r,          h - 2 * r, c);
    fb_fill_rect(x + w - r, y + r, r,          h - 2 * r, c);
    /* four rounded corners via midpoint circle */
    int cx[4] = { x + r,       x + w - 1 - r, x + r,       x + w - 1 - r };
    int cy[4] = { y + r,       y + r,          y + h - 1 - r, y + h - 1 - r };
    for (int q = 0; q < 4; q++) {
        int f = 1 - r, ddx = 0, ddy = -2 * r, px = 0, py = r;
        while (px <= py) {
            /* fill horizontal spans for each octant */
            int qx, qy;
            switch (q) {
                case 0: qx = -px; qy = -py; break;
                case 1: qx =  px; qy = -py; break;
                case 2: qx = -px; qy =  py; break;
                default: qx =  px; qy =  py; break;
            }
            fb_put_pixel(cx[q] + qx, cy[q] + qy, c);
            switch (q) {
                case 0: qx = -py; qy = -px; break;
                case 1: qx =  py; qy = -px; break;
                case 2: qx = -py; qy =  px; break;
                default: qx =  py; qy =  px; break;
            }
            fb_put_pixel(cx[q] + qx, cy[q] + qy, c);
            if (f >= 0) { py--; ddy += 2; f += ddy; }
            px++; ddx += 2; f += ddx + 1;
        }
    }
    /* solid fill for corners (simpler approach over the midpoint outline) */
    for (int ry = 0; ry <= r; ry++) {
        int half = 0;
        while (half * half + ry * ry > r * r && half > 0) half--;
        /* re-derive half-width at this ry */
        half = 0;
        while ((half + 1) * (half + 1) + ry * ry <= r * r) half++;
        /* top-left */
        fb_fill_rect(x + r - half, y + r - ry, half + 1, 1, c);
        /* top-right */
        fb_fill_rect(x + w - 1 - r, y + r - ry, half + 1, 1, c);
        /* bottom-left */
        fb_fill_rect(x + r - half, y + h - 1 - r + ry, half + 1, 1, c);
        /* bottom-right */
        fb_fill_rect(x + w - 1 - r, y + h - 1 - r + ry, half + 1, 1, c);
    }
}

void fb_draw_circle(int cx, int cy, int r, uint32_t color) {
    int x = 0, y = r, f = 1 - r, ddx = 0, ddy = -2 * r;
    fb_put_pixel(cx, cy + r, color);
    fb_put_pixel(cx, cy - r, color);
    fb_put_pixel(cx + r, cy, color);
    fb_put_pixel(cx - r, cy, color);
    while (x < y) {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx + 1;
        fb_put_pixel(cx + x, cy + y, color);
        fb_put_pixel(cx - x, cy + y, color);
        fb_put_pixel(cx + x, cy - y, color);
        fb_put_pixel(cx - x, cy - y, color);
        fb_put_pixel(cx + y, cy + x, color);
        fb_put_pixel(cx - y, cy + x, color);
        fb_put_pixel(cx + y, cy - x, color);
        fb_put_pixel(cx - y, cy - x, color);
    }
}

void fb_fill_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        int xw = 0;
        while (xw * xw + y * y <= r * r) xw++;
        fb_fill_rect(cx - xw + 1, cy + y, xw * 2 - 1, 1, color);
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, (int)fb.width, (int)fb.height, color);
}

void fb_scroll_up(int pixels, uint32_t bg_color) {
    if (pixels <= 0 || (uint32_t)pixels >= fb.height) return;
    uint8_t *dst = (uint8_t *)fb.addr;
    uint8_t *src = dst + (uint32_t)pixels * fb.pitch;
    uint32_t bytes = ((uint32_t)fb.height - (uint32_t)pixels) * fb.pitch;
    /* manual memmove (downward copy, no overlap issue src > dst) */
    for (uint32_t i = 0; i < bytes; i++)
        dst[i] = src[i];
    fb_fill_rect(0, (int)fb.height - pixels, (int)fb.width, pixels, bg_color);
}

uint32_t fb_blend(uint32_t fg, uint32_t bg, uint8_t alpha) {
    uint8_t r = (uint8_t)(((fg >> 16 & 0xFF) * (uint32_t)alpha +
                            (bg >> 16 & 0xFF) * (uint32_t)(255 - alpha)) / 255);
    uint8_t g = (uint8_t)(((fg >>  8 & 0xFF) * (uint32_t)alpha +
                            (bg >>  8 & 0xFF) * (uint32_t)(255 - alpha)) / 255);
    uint8_t b = (uint8_t)(((fg       & 0xFF) * (uint32_t)alpha +
                            (bg       & 0xFF) * (uint32_t)(255 - alpha)) / 255);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h) {
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        int srow = sy + row, drow = dy + row;
        if (srow < 0 || (uint32_t)srow >= fb.height) continue;
        if (drow < 0 || (uint32_t)drow >= fb.height) continue;
        uint32_t *s = (uint32_t *)((uint8_t *)fb.addr + (uint32_t)srow * fb.pitch) + sx;
        uint32_t *d = (uint32_t *)((uint8_t *)fb.addr + (uint32_t)drow * fb.pitch) + dx;
        for (int col = 0; col < w; col++) {
            if (sx + col < 0 || (uint32_t)(sx + col) >= fb.width) continue;
            if (dx + col < 0 || (uint32_t)(dx + col) >= fb.width) continue;
            d[col] = s[col];
        }
    }
}
