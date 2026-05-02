/* NexOS — kernel/gui/theme_app.c | Theme switcher | MIT License */
#include "theme_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>
#include <stddef.h>

int g_active_theme = 0;

const theme_def_t g_themes[THEME_COUNT] = {
    /* Catppuccin Mocha */
    {
        "Catppuccin Mocha",
        0x1E1E2E, 0x181825, 0x11111B,
        0x313244, 0x45475A, 0x585B70, 0x6C7086,
        0xCDD6F4, 0xA6ADC8,
        0x89B4FA, 0xB4BEFE, 0xCBA6F7,
        0xF38BA8, 0xFAB387, 0xF9E2AF, 0xA6E3A1,
        0x94E2D5, 0x89DCEB,
        { 0x1E1E2E, 0x89B4FA, 0xCBA6F7, 0xA6E3A1, 0xF38BA8 }
    },
    /* Nord */
    {
        "Nord",
        0x2E3440, 0x3B4252, 0x434C5E,
        0x4C566A, 0x434C5E, 0x4C566A, 0x616E88,
        0xECEFF4, 0xD8DEE9,
        0x81A1C1, 0x88C0D0, 0xB48EAD,
        0xBF616A, 0xD08770, 0xEBCB8B, 0xA3BE8C,
        0x8FBCBB, 0x88C0D0,
        { 0x2E3440, 0x81A1C1, 0xB48EAD, 0xA3BE8C, 0xBF616A }
    },
    /* Dracula */
    {
        "Dracula",
        0x282A36, 0x1E1F29, 0x191A21,
        0x44475A, 0x44475A, 0x6272A4, 0x6272A4,
        0xF8F8F2, 0xBFC7D5,
        0x6272A4, 0xBD93F9, 0xFF79C6,
        0xFF5555, 0xFFB86C, 0xF1FA8C, 0x50FA7B,
        0x8BE9FD, 0x8BE9FD,
        { 0x282A36, 0x6272A4, 0xBD93F9, 0x50FA7B, 0xFF5555 }
    },
    /* Gruvbox Dark */
    {
        "Gruvbox Dark",
        0x282828, 0x1D2021, 0x1D2021,
        0x3C3836, 0x504945, 0x665C54, 0x7C6F64,
        0xEBDBB2, 0xBDAE93,
        0x83A598, 0xB8BB26, 0xD3869B,
        0xFB4934, 0xFE8019, 0xFABD2F, 0xB8BB26,
        0x8EC07C, 0x83A598,
        { 0x282828, 0x83A598, 0xD3869B, 0xB8BB26, 0xFB4934 }
    }
};

void theme_apply(int id) {
    if (id < 0 || id >= THEME_COUNT) return;
    g_active_theme = id;
    const theme_def_t *th = &g_themes[id];
    col_base     = th->base;
    col_mantle   = th->mantle;
    col_crust    = th->crust;
    col_surface0 = th->surface0;
    col_surface1 = th->surface1;
    col_surface2 = th->surface2;
    col_overlay0 = th->overlay0;
    col_text     = th->text;
    col_subtext  = th->subtext;
    col_blue     = th->blue;
    col_lavender = th->lavender;
    col_mauve    = th->mauve;
    col_red      = th->red;
    col_peach    = th->peach;
    col_yellow   = th->yellow;
    col_green    = th->green;
    col_teal     = th->teal;
    col_sky      = th->sky;
    fb_scene_dirty = 1;   /* full repaint with new palette */
}

static void theme_paint(window_t *win) {
    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    fb_fill_rect(wx, wy, win->w, win->h - WM_TITLEBAR_H, COL_MANTLE);

    int cy = wy + 12;
    for (int i = 0; i < THEME_COUNT; i++) {
        const theme_def_t *th = &g_themes[i];
        uint32_t card_bg = (i == g_active_theme) ? COL_SURFACE1 : COL_SURFACE0;
        fb_fill_rounded_rect(wx + 16, cy, 268, 52, 8, card_bg);
        if (i == g_active_theme) {
            fb_draw_rect_outline(wx + 16, cy, 268, 52, COL_BLUE, 2);
            font_puts(wx + 16 + 268 - 56, cy + 18, "Active", COL_BLUE, card_bg);
        }
        font_puts(wx + 24, cy + 8, th->name, COL_TEXT, card_bg);
        for (int d = 0; d < 5; d++) {
            fb_fill_circle(wx + 24 + d * 22, cy + 36, 8, th->dot[d]);
        }
        cy += 60;
    }
}

static void theme_click(window_t *win, int cx, int cy, int btn) {
    (void)cx; (void)btn;
    int rel_y = cy - 12;
    int idx = rel_y / 60;
    if (idx >= 0 && idx < THEME_COUNT) theme_apply(idx);
    wm_invalidate(win);
}

static void theme_close(window_t *win) { wm_close(win); }

window_t *theme_create(int x, int y) {
    window_t *win = wm_new(x, y, 300, 300, "Themes");
    if (!win) return NULL;
    win->on_paint = theme_paint;
    win->on_click = theme_click;
    win->on_close = theme_close;
    return win;
}
