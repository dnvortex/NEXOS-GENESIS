/* NexOS — kernel/gui/launcher.c | Liquid-glass app launcher with animations
 *
 * Open:  panel slides up from below + scrim fades in  (~350 ms, ease-out-cubic)
 * Close: panel slides back down + scrim fades out      (~220 ms, ease-in-quad)
 * MIT License */
#include "launcher.h"
#include "anim.h"
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
void launch_browser(void);
void launch_calc(void);
void launch_clock(void);
void launch_editor(void);
void launch_visualizer(void);
void launch_snake(void);
void launch_sysmon(void);

static void action_restart(void) {
    __asm__ volatile("mov $0xFE, %%al\n out %%al, $0x64\n" ::: "eax");
}
static void action_shutdown(void) {
    __asm__ volatile("mov $0x2000, %%ax\n mov $0x604, %%dx\n outw %%ax, %%dx\n"
                     ::: "eax", "edx");
}

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define PANEL_W     540
#define PANEL_H     580
#define PANEL_R      20
#define CARD_COLS     4
#define CARD_W      110
#define CARD_H      112
#define CARD_R       16
#define CARD_PAD     16
#define GRID_TOP    100
#define ICON_R       30

/* ── Palette ─────────────────────────────────────────────────────────────── */
#define GLASS_SCRIM   0x0A0A18
#define GLASS_PANEL   0x1A1B2E
#define GLASS_CARD    0x252645
#define GLASS_HOVER   0x383970
#define GLASS_RIM     0x4A4B7A
#define GLASS_HILITE  0x8888CC
#define GLASS_SEP     0x35365A
#define GLASS_TITLE   0xCDD6F4
#define GLASS_HINT    0x6C7086
#define GLASS_SEARCH  0x1E2040
#define POWER_RESTART 0x3B3B6E
#define POWER_SHUT    0x4A2040

/* ── App table ───────────────────────────────────────────────────────────── */
typedef struct {
    const char *label;
    char        icon_char;
    uint32_t    icon_color;
    void      (*action)(void);
} app_item_t;

static const app_item_t apps[] = {
    { "Terminal",   'T', 0xA6E3A1, launch_terminal    },
    { "Files",      'F', 0x89B4FA, launch_filemanager },
    { "System",     'S', 0xCBA6F7, launch_sysinfo     },
    { "Theme",      'C', 0xFAB387, launch_themewin    },
    { "Browser",    'W', 0x74C7EC, launch_browser     },
    { "Calc",       '+', 0xF9E2AF, launch_calc        },
    { "Clock",      'O', 0x94E2D5, launch_clock       },
    { "Editor",     'E', 0x89DCEB, launch_editor      },
    { "Visualizer", 'V', 0xF38BA8, launch_visualizer  },
    { "Snake",      'G', 0xA6E3A1, launch_snake       },
    { "Monitor",    'M', 0xCBA6F7, launch_sysmon      },
};
#define APP_COUNT  ((int)(sizeof(apps)/sizeof(apps[0])))

/* ── State ───────────────────────────────────────────────────────────────── */
static int launcher_visible = 0;  /* 1 after show(), before real hide */
static int launcher_closing = 0;  /* 1 while close animation runs */
static int laun_anim        = 0;  /* 0-256 animation progress */
static int panel_x, panel_y;
static int launcher_hover = -1;
static int hover_restart  = 0;
static int hover_shutdown = 0;

/* Per-card hover glow (0-256 smooth transition) */
static int card_glow[APP_COUNT > 0 ? APP_COUNT : 1];

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static int lstr_len(const char *s) { int n=0; while(s[n]) n++; return n; }

static void draw_centered(int rx, int rw, int y,
                           const char *s, uint32_t fg, uint32_t bg) {
    int tx = rx + (rw - lstr_len(s) * 8) / 2;
    font_puts(tx, y, s, fg, bg);
}

static void draw_card(int cx, int cy, int idx, int hovered) {
    int cx0 = cx - CARD_W / 2;
    /* Smooth hover glow interpolation */
    uint32_t card_bg = anim_color_lerp(GLASS_CARD, GLASS_HOVER, card_glow[idx]);
    fb_fill_rounded_rect(cx0, cy, CARD_W, CARD_H, CARD_R, card_bg);

    /* Rim highlight on hover */
    if (hovered)
        fb_fill_rect_blend(cx0 + CARD_R, cy, CARD_W - 2*CARD_R, 1,
                           0x7070B0, (uint8_t)(card_glow[idx] * 200 / 256));

    /* Icon */
    int icon_cy = cy + ICON_R + 14;
    fb_fill_circle(cx, icon_cy, ICON_R, apps[idx].icon_color);
    uint32_t hi = fb_blend(0xFFFFFF, apps[idx].icon_color, 100);
    fb_fill_circle(cx - 4, icon_cy - 6, ICON_R - 8,  hi);
    fb_fill_circle(cx - 4, icon_cy - 6, ICON_R - 12, apps[idx].icon_color);
    char ic[2] = { apps[idx].icon_char, 0 };
    font_puts(cx - 4, icon_cy - 5, ic, 0x0D0D1A, apps[idx].icon_color);

    draw_centered(cx0, CARD_W, cy + CARD_H - 24,
                  apps[idx].label, GLASS_TITLE, card_bg);
}

/* ── Animation tick ──────────────────────────────────────────────────────── */
void launcher_tick(uint32_t delta_ms) {
    /* Update card hover glow */
    for (int i = 0; i < APP_COUNT; i++) {
        int target = (launcher_hover == i) ? 256 : 0;
        int step   = (int)(delta_ms * 256 / 120);   /* ~120 ms full transition */
        if (card_glow[i] < target)
            card_glow[i] = anim_clamp(card_glow[i] + step, 0, target);
        else if (card_glow[i] > target)
            card_glow[i] = anim_clamp(card_glow[i] - step, target, 256);
    }

    if (!launcher_visible && !launcher_closing) return;

    if (launcher_closing) {
        /* Close: ease-in-quad — starts slow, then snaps away */
        int step = (int)(delta_ms * 256 / 200);  /* ~200 ms close */
        laun_anim -= step;
        if (laun_anim <= 0) {
            laun_anim       = 0;
            launcher_closing = 0;
            launcher_visible = 0;
            fb_scene_dirty   = 1;
        }
    } else {
        /* Open: ease-out-cubic — shoots up then decelerates */
        int step = (int)(delta_ms * 256 / 350);  /* ~350 ms open */
        laun_anim = anim_clamp(laun_anim + step, 0, 256);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void launcher_show(int x, int y) {
    (void)x; (void)y;
    panel_x = ((int)fb.width  - PANEL_W) / 2;
    panel_y = ((int)fb.height - PANEL_H) / 2;
    if (panel_y < 4) panel_y = 4;
    launcher_visible = 1;
    launcher_closing = 0;
    laun_anim        = 0;   /* start animation from 0 */
    launcher_hover   = -1;
    hover_restart    = 0;
    hover_shutdown   = 0;
    for (int i = 0; i < APP_COUNT; i++) card_glow[i] = 0;
    fb_scene_dirty   = 1;
}

void launcher_hide(void) {
    if (launcher_visible && !launcher_closing) {
        launcher_closing = 1;  /* start close animation — tick will finish */
    }
}

int launcher_is_visible(void) {
    return launcher_visible || launcher_closing;
}

void launcher_draw(void) {
    if (!launcher_visible && !launcher_closing) return;

    /* ── Animation values ── */
    int   p_open  = anim_ease_out_cubic(laun_anim);
    int   p_close = anim_ease_in_quad(laun_anim);
    int   p       = launcher_closing ? p_close : p_open;

    /* Scrim alpha: 0 → 190 on open, 190 → 0 on close */
    uint8_t scrim_a = (uint8_t)(190 * p / 256);

    /* Panel vertical offset: slides up from below */
    int y_lift = (PANEL_H / 3) * (256 - p) / 256;
    int py     = panel_y + y_lift;

    /* Panel contents alpha */
    uint8_t panel_a = (uint8_t)(215 * p / 256);
    if (panel_a < 4) return;

    /* ── Step 1: scrim ── */
    int desktop_h = (int)fb.height - 40;
    if (scrim_a > 2)
        fb_fill_rect_blend(0, 0, (int)fb.width, desktop_h, GLASS_SCRIM, scrim_a);

    /* ── Step 2: frosted panel ── */
    if (panel_a > 8) {
        fb_blur_rect(panel_x + 2, py + 2, PANEL_W - 4, PANEL_H - 4, 6);
        fb_fill_rect_blend(panel_x, py, PANEL_W, PANEL_H, GLASS_PANEL, panel_a);
        fb_fill_rounded_rect(panel_x, py, PANEL_W, PANEL_H, PANEL_R, GLASS_PANEL);
    }

    /* ── Step 3: rim + specular ── */
    fb_draw_rect_outline(panel_x, py, PANEL_W, PANEL_H, GLASS_RIM, 1);
    fb_fill_rect(panel_x + PANEL_R, py + 1,
                 PANEL_W - 2 * PANEL_R, 1, GLASS_HILITE);

    /* ── Step 4: title ── */
    draw_centered(panel_x, PANEL_W, py + 18,
                  "NexOS  Apps", GLASS_TITLE, GLASS_PANEL);

    /* ── Step 5: search bar ── */
    int sb_x = panel_x + 20, sb_y = py + 46, sb_w = PANEL_W - 40, sb_h = 28;
    fb_fill_rounded_rect(sb_x, sb_y, sb_w, sb_h, 8, GLASS_SEARCH);
    fb_draw_rect_outline(sb_x, sb_y, sb_w, sb_h, GLASS_RIM, 1);
    font_puts(sb_x + 12, sb_y + 8, "  Search apps...", GLASS_HINT, GLASS_SEARCH);

    /* ── Step 6: app grid ── */
    int total_w = CARD_COLS * CARD_W + (CARD_COLS - 1) * CARD_PAD;
    int grid_x0 = panel_x + (PANEL_W - total_w) / 2;
    int grid_y  = py + GRID_TOP;

    for (int i = 0; i < APP_COUNT && i < CARD_COLS; i++) {
        int card_cx = grid_x0 + i * (CARD_W + CARD_PAD) + CARD_W / 2;
        draw_card(card_cx, grid_y, i, launcher_hover == i);
    }
    for (int i = CARD_COLS; i < APP_COUNT; i++) {
        int col     = i - CARD_COLS;
        int card_cx = grid_x0 + col * (CARD_W + CARD_PAD) + CARD_W / 2;
        draw_card(card_cx, grid_y + CARD_H + CARD_PAD, i, launcher_hover == i);
    }

    /* ── Step 7: separator ── */
    int rows  = (APP_COUNT + CARD_COLS - 1) / CARD_COLS;
    int sep_y = py + GRID_TOP + rows * (CARD_H + CARD_PAD) - CARD_PAD + 16;
    fb_fill_rect(panel_x + 20, sep_y, PANEL_W - 40, 1, GLASS_SEP);

    /* ── Step 8: power buttons ── */
    int pbtn_y  = sep_y + 12;
    int pbtn_w  = 150, pbtn_h = 36, pbtn_r = 10;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbtn_w - 8;
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;

    uint32_t rb = hover_restart  ? fb_blend(POWER_RESTART, 0xFFFFFF, 40) : POWER_RESTART;
    fb_fill_rounded_rect(pbtn_gx, pbtn_y, pbtn_w, pbtn_h, pbtn_r, rb);
    fb_draw_rect_outline(pbtn_gx, pbtn_y, pbtn_w, pbtn_h, GLASS_RIM, 1);
    draw_centered(pbtn_gx, pbtn_w, pbtn_y + 12, "  Restart", 0xF9E2AF, rb);

    uint32_t sb2 = hover_shutdown ? fb_blend(POWER_SHUT, 0xFFFFFF, 40) : POWER_SHUT;
    fb_fill_rounded_rect(pbs_x, pbtn_y, pbtn_w, pbtn_h, pbtn_r, sb2);
    fb_draw_rect_outline(pbs_x, pbtn_y, pbtn_w, pbtn_h, GLASS_RIM, 1);
    draw_centered(pbs_x, pbtn_w, pbtn_y + 12, "  Shutdown", 0xF38BA8, sb2);
}

void launcher_handle_click(int x, int y) {
    if (!launcher_visible) return;
    int py = panel_y;   /* use resting position for hit-testing */

    if (x < panel_x || x >= panel_x + PANEL_W ||
        y < py      || y >= py      + PANEL_H) {
        launcher_hide(); return;
    }

    int total_w = CARD_COLS * CARD_W + (CARD_COLS - 1) * CARD_PAD;
    int grid_x0 = panel_x + (PANEL_W - total_w) / 2;
    int grid_y  = py + GRID_TOP;

    for (int i = 0; i < APP_COUNT; i++) {
        int row = i / CARD_COLS, col = i % CARD_COLS;
        int cx0 = grid_x0 + col * (CARD_W + CARD_PAD);
        int cy0 = grid_y  + row * (CARD_H + CARD_PAD);
        if (x >= cx0 && x < cx0 + CARD_W && y >= cy0 && y < cy0 + CARD_H) {
            if (apps[i].action) apps[i].action();
            launcher_hide(); return;
        }
    }

    int rows_c = (APP_COUNT + CARD_COLS - 1) / CARD_COLS;
    int sep_y  = py + GRID_TOP + rows_c * (CARD_H + CARD_PAD) - CARD_PAD + 16;
    int pby    = sep_y + 12, pbw = 150, pbh = 36;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbw - 8;
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;
    if (y >= pby && y < pby + pbh) {
        if (x >= pbtn_gx && x < pbtn_gx + pbw) { action_restart();  return; }
        if (x >= pbs_x   && x < pbs_x   + pbw) { action_shutdown(); return; }
    }
    launcher_hide();
}

void launcher_handle_mouse(int x, int y) {
    if (!launcher_visible) return;
    int prev_h   = launcher_hover;
    int prev_r   = hover_restart;
    int prev_s   = hover_shutdown;

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
        if (x >= cx0 && x < cx0 + CARD_W && y >= cy0 && y < cy0 + CARD_H)
            { launcher_hover = i; break; }
    }

    int rows_m = (APP_COUNT + CARD_COLS - 1) / CARD_COLS;
    int sep_y  = panel_y + GRID_TOP + rows_m * (CARD_H + CARD_PAD) - CARD_PAD + 16;
    int pby    = sep_y + 12, pbw = 150, pbh = 36;
    int pbtn_gx = panel_x + (PANEL_W / 2) - pbw - 8;
    int pbs_x   = panel_x + (PANEL_W / 2) + 8;
    if (y >= pby && y < pby + pbh) {
        if (x >= pbtn_gx && x < pbtn_gx + pbw) hover_restart  = 1;
        if (x >= pbs_x   && x < pbs_x   + pbw) hover_shutdown = 1;
    }

    if (launcher_hover != prev_h || hover_restart != prev_r ||
        hover_shutdown != prev_s)
        fb_scene_dirty = 1;
}

void launcher_handle_key(char key) {
    if (key == 27) { launcher_hide(); return; }
}
