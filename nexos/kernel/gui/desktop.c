/* NexOS — kernel/gui/desktop.c | Desktop background | MIT License */
#include "desktop.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"

void desktop_draw(void) {
    if (!fb.initialized) return;
    int desk_h = (int)fb.height - 40;
    /* gradient top→bottom COL_MANTLE→COL_BASE */
    for (int y = 0; y < desk_h; y++) {
        uint8_t t = (uint8_t)((uint32_t)y * 255 / (uint32_t)desk_h);
        uint32_t c = fb_blend(COL_BASE, COL_MANTLE, t);
        for (int x = 0; x < (int)fb.width; x++)
            fb_put_pixel(x, y, c);
    }
    /* subtle dot grid */
    for (int y = 0; y < desk_h; y += 32)
        for (int x = 0; x < (int)fb.width; x += 32)
            fb_put_pixel(x, y, COL_SURFACE0);
    /* watermark */
    font_puts2x((int)fb.width - 150, desk_h - 40,
                "NexOS 0.1", COL_SURFACE0, 0);
}
