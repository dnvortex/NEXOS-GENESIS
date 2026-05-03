/* NexOS — kernel/gui/desktop.c
 * Desktop background with animated aurora gradient.
 * The aurora slowly oscillates between two hue shifts creating a living,
 * breathing background effect without constant redraws.
 * MIT License */
#include "desktop.h"
#include "anim.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"

/* ── Aurora phase (updated by gui.c every ~2 s) ─────────────────────────── */
static uint32_t aurora_ms = 0;   /* monotonic ms accumulator */

void desktop_set_phase(uint32_t delta_ms) {
    aurora_ms += delta_ms;
}

/* ── Full desktop repaint ────────────────────────────────────────────────── */
void desktop_draw(void) {
    if (!fb.initialized) return;
    int desk_h = (int)fb.height - 40;
    int desk_w = (int)fb.width;

    /* Aurora oscillation: triangle wave 0-256 over 8-second period */
    int aurora = anim_pingpong(aurora_ms, 8000);

    /* Gradient top/bottom colours breathe between two subtle hues */
    uint32_t c_top = anim_color_lerp(COL_BASE,   0x11112A, aurora / 4);
    uint32_t c_bot = anim_color_lerp(COL_MANTLE, 0x1E1040, aurora / 3);

    /* Full-height gradient */
    for (int y = 0; y < desk_h; y++) {
        uint8_t  t = (uint8_t)((uint32_t)y * 255 / (uint32_t)desk_h);
        uint32_t c = fb_blend(c_top, c_bot, t);
        for (int x = 0; x < desk_w; x++)
            fb_put_pixel(x, y, c);
    }

    /* Subtle horizontal aurora band that drifts slowly down the screen */
    int band_y  = (int)((uint32_t)(aurora_ms % 12000) * (uint32_t)desk_h / 12000u);
    uint8_t band_intensity = (uint8_t)(aurora / 8 + 6);   /* 6-38 alpha */
    for (int dy = 0; dy < 24; dy++) {
        int gy = band_y + dy - 12;
        if (gy < 0 || gy >= desk_h) continue;
        /* Gaussian-shaped falloff: bright at dy=12, dim at edges */
        int dist  = (dy < 12) ? (12 - dy) : (dy - 12);
        uint8_t a = (uint8_t)(band_intensity - (uint8_t)(dist * band_intensity / 13));
        if (a > 2)
            fb_fill_rect_blend(0, gy, desk_w, 1, COL_LAVENDER, a);
    }

    /* Dot grid */
    for (int y = 0; y < desk_h; y += 32)
        for (int x = 0; x < desk_w; x += 32)
            fb_put_pixel(x, y, COL_SURFACE0);

    /* Watermark */
    font_puts2x(desk_w - 150, desk_h - 40, "NexOS 0.1", COL_SURFACE0, 0);
}

/* ── Surgical region repaint ─────────────────────────────────────────────── */
void desktop_paint_rect(int x, int y, int w, int h) {
    if (!fb.initialized) return;
    int desk_h = (int)fb.height - 40;
    int desk_w = (int)fb.width;

    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w) > desk_w ? desk_w : (x + w);
    int y2 = (y + h) > desk_h ? desk_h : (y + h);
    if (x1 >= x2 || y1 >= y2) return;

    int aurora = anim_pingpong(aurora_ms, 8000);
    uint32_t c_top = anim_color_lerp(COL_BASE,   0x11112A, aurora / 4);
    uint32_t c_bot = anim_color_lerp(COL_MANTLE, 0x1E1040, aurora / 3);

    for (int py = y1; py < y2; py++) {
        uint8_t  t = (uint8_t)((uint32_t)py * 255 / (uint32_t)desk_h);
        uint32_t c = fb_blend(c_top, c_bot, t);
        for (int px = x1; px < x2; px++)
            fb_put_pixel(px, py, c);
    }

    int gx0 = (x1 / 32) * 32;
    int gy0 = (y1 / 32) * 32;
    for (int gy = gy0; gy < y2; gy += 32)
        for (int gx = gx0; gx < x2; gx += 32)
            if (gx >= x1 && gy >= y1)
                fb_put_pixel(gx, gy, COL_SURFACE0);
}
