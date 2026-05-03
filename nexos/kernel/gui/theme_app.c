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
    col_base     = th->base;     col_mantle   = th->mantle;
    col_crust    = th->crust;    col_surface0 = th->surface0;
    col_surface1 = th->surface1; col_surface2 = th->surface2;
    col_overlay0 = th->overlay0; col_text     = th->text;
    col_subtext  = th->subtext;  col_blue     = th->blue;
    col_lavender = th->lavender; col_mauve    = th->mauve;
    col_red      = th->red;      col_peach    = th->peach;
    col_yellow   = th->yellow;   col_green    = th->green;
    col_teal     = th->teal;     col_sky      = th->sky;
    fb_scene_dirty = 1;
}

static void theme_paint(window_t *win) {
    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;
    fb_fill_rect(wx, wy, ww, win->h - WM_TITLEBAR_H, COL_MANTLE);

    /* Header */
    fb_fill_rect(wx, wy, ww, 36, COL_SURFACE0);
    fb_fill_rect(wx, wy, 4, 36, COL_MAUVE);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 18);
    font_puts(wx + 14, wy + 10, "Choose a theme", COL_TEXT, COL_SURFACE0);
    fb_fill_rect(wx, wy + 36, ww, 1, COL_SURFACE1);

    int cy = wy + 44;
    int card_w = ww - 24;

    for (int i = 0; i < THEME_COUNT; i++) {
        const theme_def_t *th = &g_themes[i];
        int active   = (i == g_active_theme);
        int card_h   = 72;
        uint32_t cbg = active ? COL_SURFACE1 : COL_SURFACE0;

        /* Card background */
        fb_fill_rounded_rect(wx + 12, cy, card_w, card_h, 10, cbg);

        /* Active: blue outline + specular top rim */
        if (active) {
            fb_draw_rect_outline(wx + 12, cy, card_w, card_h, COL_BLUE, 2);
            fb_fill_rect_blend(wx + 14, cy + 1, card_w - 4, 1, 0xFFFFFF, 20);
        }

        /* Left accent bar — theme's signature accent color */
        fb_fill_rounded_rect(wx + 12, cy, 4, card_h, 4, th->blue);

        /* Theme name */
        font_puts(wx + 24, cy + 10, th->name,
                  active ? COL_TEXT : COL_SUBTEXT, cbg);

        /* Active badge */
        if (active) {
            int bw = 52, bx2 = wx + 12 + card_w - bw - 8;
            fb_fill_rounded_rect(bx2, cy + 8, bw, 18, 6, COL_BLUE);
            font_puts(bx2 + 8, cy + 12, "Active", COL_BASE, COL_BLUE);
        }

        /* 8-color full-width palette strip */
        uint32_t sw[8] = {
            th->red, th->peach, th->yellow, th->green,
            th->teal, th->sky, th->blue, th->mauve
        };
        int sx0   = wx + 24;
        int sw_w  = card_w - 30;       /* total strip width */
        int sw_ea = sw_w / 8;          /* each swatch */
        int sy    = cy + 34;

        for (int s = 0; s < 8; s++) {
            int sx = sx0 + s * sw_ea;
            fb_fill_rounded_rect(sx, sy, sw_ea - 2, 20,
                                 (s == 0 || s == 7) ? 4 : 0, sw[s]);
        }
        /* Specular shimmer on strip */
        fb_fill_rect_blend(sx0, sy, sw_w - 2, 9, 0xFFFFFF, 22);

        cy += card_h + 8;
    }
}

static void theme_click(window_t *win, int cx, int cy, int btn) {
    (void)cx; (void)btn;
    /* offset from client area top, skip header */
    int rel_y = cy - 44;
    if (rel_y < 0) return;
    int idx = rel_y / 80;   /* card_h(72) + gap(8) = 80 */
    if (idx >= 0 && idx < THEME_COUNT) theme_apply(idx);
    wm_invalidate(win);
}

static void theme_close(window_t *win) { wm_close(win); }

window_t *theme_create(int x, int y) {
    window_t *win = wm_new(x, y, 310, 420, "Themes");
    if (!win) return NULL;
    win->on_paint = theme_paint;
    win->on_click = theme_click;
    win->on_close = theme_close;
    return win;
}
