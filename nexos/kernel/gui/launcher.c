/* NexOS — kernel/gui/launcher.c | Liquid-glass app launcher | MIT License
 *
 * Full-screen overlay with frosted-glass panel, blurred background,
 * 4-column app grid, and translucent power strip.
 */
#include "launcher.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>
#include <stddef.h>

/* ── App launchers (defined elsewhere) ──────────────────────────────────── */
void launch_terminal(void);
void launch_filemanager(void);
void launch_sysinfo(void);
void launch_themewin(void);

static void action_restart(void) {
    __asm__ volatile("mov $0xFE, %%al\n out %%al, $0x64\n" ::: "eax");
}
static void action_shutdown(void) {
    __asm__ volatile("mov $0x2000, %%ax\n mov $0x604, %%dx\n outw %%ax, %%dx\n"
                     ::: "eax", "edx");
}

/* ── Layout constants ────────────────────────────────────────────────────── */
#define PANEL_W     540
#define PANEL_H     420
#define PANEL_R     20    /* corner radius */
#define CARD_COLS   4
#define CARD_W      110
#define CARD_H      112
#define CARD_R      16
#define CARD_PAD    16    /* horizontal gap between cards */
#define GRID_TOP    100   /* panel-relative y of first card row */
#define ICON_R      30    /* app icon circle radius */

/* ── Colour palette ──────────────────────────────────────────────────────── */
#define GLASS_SCRIM   0x0A0A18   /* dark navy scrim */
#define GLASS_PANEL   0x1A1B2E   /* frosted panel base */
#define GLASS_CARD    0x252645   /* card background */
#define GLASS_HOVER   0x383970   /* card hover highlight */
#define GLASS_RIM     0x4A4B7A   /* panel border */
#define GLASS_HILITE  0x8888CC   /* top specular line */
#define GLASS_SEP     0x35365A   /* separator line */
#define GLASS_TITLE   0xCDD6F4   /* title text */
#define GLASS_HINT    0x6C7086   /* search hint text */
#define GLASS_SEARCH  0x1E2040   /* search bar background */
#define POWER_RESTART 0x3B3B6E   /* restart button bg */
#define POWER_SHUT    0x4A2040   /* shutdown button bg (warm red tint) */

/* ── App item definition ─────────────────────────────────────────────────── */
typedef struct {
    const char *label;
    char        icon_char;
    uint32_t    icon_color;
    void      (*action)(void);
} app_item_t;

static const app_item_t apps[] = {
    { "Terminal",   'T', 0xA6E3A1, launch_terminal   },
    { "Files",      'F', 0x89B4FA, launch_filemanager},
    { "System",     'S', 0xCBA6F7, launch_sysinfo    },
    { "Theme",      'C', 0xFAB387, launch_themewin   },
};
#define APP_COUNT  ((int)(sizeof(apps)/sizeof(apps[0])))

/* ── State ───────────────────────────────────────────────────────────────── */
static int launcher_visible = 0;
static int panel_x, panel_y;
static int launcher_hover = -1;   /* hovered app index, -1 = none */
static int hover_restart  = 0;
static int hover_shutdown = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static int lstr_len(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

/* Draw centered text inside a rect (uses 8px wide chars from font module) */
static void draw_centered(int rect_x, int rect_w, int y,
                           const char *s, uint32_t fg, uint32_t bg) {
    int slen = lstr_len(s) * 8;
    int tx   = rect_x + (rect_w - slen) / 2;
    font_puts(tx, y, s, fg, bg);
}

/* Draw a single app card at absolute position (cx=center-x, cy=top-y) */
static void draw_card(int cx, int cy, int idx, int hovered) {
    int cx0 = cx - CARD_W / 2;
    uint32_t card_bg = hovered ? GLASS_HOVER : GLASS_CARD;

    /* Card background */
    fb_fill_rounded_rect(cx0, cy, CARD_W, CARD_H, CARD_R, card_bg);

    /* Subtle top rim highlight on hover */
    if (hovered)
        fb_fill_rect(cx0 + CARD_R, cy, CARD_W - 2 * CARD_R, 1, 0x6060A0);

    /* Icon circle */
    int icon_cy = cy + ICON_R + 14;
    fb_fill_circle(cx, icon_cy, ICON_R, apps[idx].icon_color);

    /* Inner specular: small bright arc at top of circle (1px lighter ring) */
    uint32_t hi_col = fb_blend(0xFFFFFF, apps[idx].icon_color, 100);
    fb_fill_circle(cx - 4, icon_cy - 6, ICON_R - 8, hi_col);
    fb_fill_circle(cx - 4, icon_cy - 6, ICON_R - 12, apps[idx].icon_color);

    /* Icon letter */
    char ic[2] = { apps[idx].icon_char, 0 };
    font_puts(cx - 4, icon_cy - 5, ic, 0x0D0D1A, apps[idx].icon_color);

    /* Label below icon */
    draw_centered(cx0, CARD_W, cy + CARD_H - 24,
                  apps[idx].label, GLASS_TITLE, card_bg);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void launcher_show(int x, int y) {
    (void)x; (void)y;
    panel_x = ((int)fb.width  - PANEL_W) / 2;
    panel_y = ((int)fb.height - PANEL_H) / 2;
    if (panel_y < 4) panel_y = 4;
    launcher_visible = 1;
    launcher_hover   = -1;
    hover_restart    = 0;
    hover_shutdown   = 0;
    fb_scene_dirty   = 1;
}

void launcher_hide(void) {
    if (launcher_visible) {
        launcher_visible = 0;
        fb_scene_dirty   = 1;
    }
}

int  launcher_is_visible(void) { return launcher_visible; }

void launcher_draw(void) {
    if (!launcher_visible) return;

    /* ── Step 1: dark scrim over the full desktop area ─────────────────── */
    int desktop_h = (int)fb.height - 40; /* exclude taskbar */
    fb_fill_rect_blend(0, 0, (int)fb.width, desktop_h, GLASS_SCRIM, 195);

    /* ── Step 2: frosted glass panel ────────────────────────────────────── */
    /* Blur the background under the panel for the frosted effect */
    fb_blur_rect(panel_x + 2, panel_y + 2, PANEL_W - 4, PANEL_H - 4, 6);
    /* Tint the blurred area with the panel glass color */
    fb_fill_rect_blend(panel_x, panel_y, PANEL_W, PANEL_H, GLASS_PANEL, 210);
    /* Rounded corners on top of the blend (solid corners) */
    fb_fill_rounded_rect(panel_x, panel_y, PANEL_W, PANEL_H, PANEL_R, GLASS_PANEL);

    /* ── Step 3: panel rim / border ─────────────────────────────────────── */
    fb_draw_rect_outline(panel_x, panel_y, PANEL_W, PANEL_H, GLASS_RIM, 1);
    /* Top specular highlight (1px bright line just inside top border) */
    fb_fill_rect(panel_x + PANEL_R, panel_y + 1,
                 PANEL_W - 2 * PANEL_R, 1, GLASS_HILITE);

    /* ── Step 4: title bar ───────────────────────────────────────────────── */
    int title_y = panel_y + 18;
    draw_centered(panel_x, PANEL_W, title_y, "NexOS  Apps", GLASS_TITLE, GLASS_PANEL);

    /* ── Step 5: search bar (decorative) ────────────────────────────────── */
    int sb_x = panel_x + 20, sb_y = panel_y + 46, sb_w = PANEL_W - 40, sb_h = 28;
    fb_fill_rounded_rect(sb_x, sb_y, sb_w, sb_h, 8, GLASS_SEARCH);
    fb_draw_rect_outline(sb_x, sb_y, sb_w, sb_h, GLASS_RIM, 1);
    font_puts(sb_x + 12, sb_y + 8, "  Search apps...", GLASS_HINT, GLASS_SEARCH);

    /* ── Step 6: app grid ────────────────────────────────────────────────── */
    /* Centre the 4-card row inside the panel */
    int total_w = CARD_COLS * CARD_W + (CARD_COLS - 1) * CARD_PAD;
    int grid_x0 = panel_x + (PANEL_W - total_w) / 2;   /* left edge of first card */
    int grid_y  = panel_y + GRID_TOP;

    for (int i = 0; i < APP_COUNT && i < CARD_COLS; i++) {
        int card_cx = grid_x0 + i * (CARD_W + CARD_PAD) + CARD_W / 2;
        draw_card(card_cx, grid_y, i, launcher_hover == i);
    }
    /* Second row if APP_COUNT > CARD_COLS */
    for (int i = CARD_COLS; i < APP_COUNT; i++) {
        int col    = i - CARD_COLS;
        int card_cx = grid_x0 + col * (CARD_W + CARD_PAD) + CARD_W / 2;
        draw_card(card_cx, grid_y + CARD_H + CARD_PAD, i, launcher_hover == i);
    }

    /* ── Step 7: separator ───────────────────────────────────────────────── */
    int sep_y = panel_y + GRID_TOP + CARD_H + (APP_COUNT > CARD_COLS ? CARD_H + CARD_PAD : 0) + 16;
    fb_fill_rect(panel_x + 20, sep_y, PANEL_W - 40, 1, GLASS_SEP);

    /* ── Step 8: power buttons ───────────────────────────────────────────── */
    int pbtn_y  = sep_y + 12;
    int pbtn_w  = 150, pbtn_h = 36, pbtn_r = 10;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbtn_w - 8;   /* left btn x */
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;             /* right btn x */

    /* Restart */
    uint32_t rb = hover_restart  ? fb_blend(POWER_RESTART, 0xFFFFFF, 40) : POWER_RESTART;
    fb_fill_rounded_rect(pbtn_gx, pbtn_y, pbtn_w, pbtn_h, pbtn_r, rb);
    fb_draw_rect_outline(pbtn_gx, pbtn_y, pbtn_w, pbtn_h, GLASS_RIM, 1);
    draw_centered(pbtn_gx, pbtn_w, pbtn_y + 12, "  Restart", 0xF9E2AF, rb);

    /* Shutdown */
    uint32_t sb2 = hover_shutdown ? fb_blend(POWER_SHUT, 0xFFFFFF, 40) : POWER_SHUT;
    fb_fill_rounded_rect(pbs_x, pbtn_y, pbtn_w, pbtn_h, pbtn_r, sb2);
    fb_draw_rect_outline(pbs_x, pbtn_y, pbtn_w, pbtn_h, GLASS_RIM, 1);
    draw_centered(pbs_x, pbtn_w, pbtn_y + 12, "  Shutdown", 0xF38BA8, sb2);
}

void launcher_handle_click(int x, int y) {
    if (!launcher_visible) return;

    /* Click outside panel → close */
    if (x < panel_x || x >= panel_x + PANEL_W ||
        y < panel_y || y >= panel_y + PANEL_H) {
        launcher_hide(); return;
    }

    /* App grid hit-test */
    int total_w = CARD_COLS * CARD_W + (CARD_COLS - 1) * CARD_PAD;
    int grid_x0 = panel_x + (PANEL_W - total_w) / 2;
    int grid_y  = panel_y + GRID_TOP;

    for (int i = 0; i < APP_COUNT; i++) {
        int row = i / CARD_COLS, col = i % CARD_COLS;
        int cx0 = grid_x0 + col * (CARD_W + CARD_PAD);
        int cy0 = grid_y  + row * (CARD_H + CARD_PAD);
        if (x >= cx0 && x < cx0 + CARD_W && y >= cy0 && y < cy0 + CARD_H) {
            if (apps[i].action) apps[i].action();
            launcher_hide(); return;
        }
    }

    /* Power buttons */
    int sep_y  = panel_y + GRID_TOP + CARD_H + 16;
    int pbtn_y = sep_y + 12, pbtn_w = 150, pbtn_h = 36;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbtn_w - 8;
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;

    if (y >= pbtn_y && y < pbtn_y + pbtn_h) {
        if (x >= pbtn_gx && x < pbtn_gx + pbtn_w) { action_restart();  return; }
        if (x >= pbs_x   && x < pbs_x   + pbtn_w) { action_shutdown(); return; }
    }

    /* Clicked inside panel but hit nothing — dismiss */
    launcher_hide();
}

void launcher_handle_mouse(int x, int y) {
    if (!launcher_visible) return;

    int prev_hover = launcher_hover;
    int prev_rest  = hover_restart;
    int prev_shut  = hover_shutdown;

    launcher_hover = -1;
    hover_restart  = 0;
    hover_shutdown = 0;

    int total_w = CARD_COLS * CARD_W + (CARD_COLS - 1) * CARD_PAD;
    int grid_x0 = panel_x + (PANEL_W - total_w) / 2;
    int grid_y  = panel_y + GRID_TOP;

    for (int i = 0; i < APP_COUNT; i++) {
        int row = i / CARD_COLS, col = i % CARD_COLS;
        int cx0 = grid_x0 + col * (CARD_W + CARD_PAD);
        int cy0 = grid_y  + row * (CARD_H + CARD_PAD);
        if (x >= cx0 && x < cx0 + CARD_W && y >= cy0 && y < cy0 + CARD_H) {
            launcher_hover = i; break;
        }
    }

    int sep_y  = panel_y + GRID_TOP + CARD_H + 16;
    int pbtn_y = sep_y + 12, pbtn_w = 150, pbtn_h = 36;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbtn_w - 8;
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;
    if (y >= pbtn_y && y < pbtn_y + pbtn_h) {
        if (x >= pbtn_gx && x < pbtn_gx + pbtn_w) hover_restart  = 1;
        if (x >= pbs_x   && x < pbs_x   + pbtn_w) hover_shutdown = 1;
    }

    if (launcher_hover != prev_hover ||
        hover_restart  != prev_rest  ||
        hover_shutdown != prev_shut)
        fb_scene_dirty = 1;
}

void launcher_handle_key(char key) {
    if (key == 27) { launcher_hide(); return; } /* Escape */
}
