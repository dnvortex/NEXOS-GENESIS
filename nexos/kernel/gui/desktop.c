/* NexOS — kernel/gui/desktop.c | Desktop background | MIT License */
#include "desktop.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"

/* ── Full desktop repaint ────────────────────────────────────────────────── */
void desktop_draw(void) {
    if (!fb.initialized) return;
    int desk_h = (int)fb.height - 40;
    for (int y = 0; y < desk_h; y++) {
        uint8_t t = (uint8_t)((uint32_t)y * 255 / (uint32_t)desk_h);
        uint32_t c = fb_blend(COL_BASE, COL_MANTLE, t);
        for (int x = 0; x < (int)fb.width; x++)
            fb_put_pixel(x, y, c);
    }
    for (int y = 0; y < desk_h; y += 32)
        for (int x = 0; x < (int)fb.width; x += 32)
            fb_put_pixel(x, y, COL_SURFACE0);
    font_puts2x((int)fb.width - 150, desk_h - 40,
                "NexOS 0.1", COL_SURFACE0, 0);
}

/* ── Surgical region repaint (for drag expose) ───────────────────────────── */
void desktop_paint_rect(int x, int y, int w, int h) {
    if (!fb.initialized) return;
    int desk_h = (int)fb.height - 40;
    int desk_w = (int)fb.width;

    /* clamp to desktop area */
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w) > desk_w  ? desk_w  : (x + w);
    int y2 = (y + h) > desk_h  ? desk_h  : (y + h);
    if (x1 >= x2 || y1 >= y2) return;

    /* gradient — replicate the same formula as desktop_draw() */
    for (int py = y1; py < y2; py++) {
        uint8_t t = (uint8_t)((uint32_t)py * 255 / (uint32_t)desk_h);
        uint32_t c = fb_blend(COL_BASE, COL_MANTLE, t);
        for (int px = x1; px < x2; px++)
            fb_put_pixel(px, py, c);
    }

    /* dot grid — only dots whose grid-aligned position falls in the region */
    int gx0 = (x1 / 32) * 32;
    int gy0 = (y1 / 32) * 32;
    for (int gy = gy0; gy < y2; gy += 32)
        for (int gx = gx0; gx < x2; gx += 32)
            if (gx >= x1 && gy >= y1)
                fb_put_pixel(gx, gy, COL_SURFACE0);
}
