/* NexOS — kernel/gui/sysinfo_app.c | System Information window | MIT License */
#include "sysinfo_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/timer.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

/* ── String helpers ──────────────────────────────────────────────────────── */
static void u64tostr(uint64_t v, char *buf) {
    char t[24]; int ti = 0;
    if (v == 0) { t[ti++] = '0'; }
    while (v) { t[ti++] = '0' + (int)(v % 10); v /= 10; }
    int bi = 0; while (ti > 0) buf[bi++] = t[--ti]; buf[bi] = 0;
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void fmt_mb(uint64_t mb, char *buf) {
    u64tostr(mb, buf);
    int n = slen(buf);
    buf[n++] = 'M'; buf[n++] = 'B'; buf[n] = 0;
}

static void fmt_pct(int pct, char *buf) {
    u64tostr((uint64_t)pct, buf);
    int n = slen(buf);
    buf[n++] = '%'; buf[n] = 0;
}

static void get_uptime_str(char *buf) {
    uint64_t s = timer_get_uptime_seconds();
    uint64_t h = s / 3600; s %= 3600;
    uint64_t m = s / 60;   s %= 60;
    char tmp[8]; int bi = 0, i;
    u64tostr(h, tmp); i=0; while (tmp[i]) buf[bi++]=tmp[i++];
    buf[bi++]='h'; buf[bi++]=' ';
    u64tostr(m, tmp); i=0; while (tmp[i]) buf[bi++]=tmp[i++];
    buf[bi++]='m'; buf[bi++]=' ';
    u64tostr(s, tmp); i=0; while (tmp[i]) buf[bi++]=tmp[i++];
    buf[bi++]='s'; buf[bi]=0;
}

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void sysinfo_paint(window_t *win) {
    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;

    fb_fill_rect(wx, wy, ww, win->h - WM_TITLEBAR_H, COL_MANTLE);

    /* ── Banner header ── */
    int hdr_h = 54;
    fb_fill_rect(wx, wy, ww, hdr_h, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 18);       /* top rim     */
    fb_fill_rect(wx, wy, 4, hdr_h, COL_BLUE);              /* left accent */
    font_puts2x(wx + 16, wy + 8, "NexOS", COL_BLUE, COL_SURFACE0);
    font_puts(wx + 16 + 5*16 + 10, wy + 20,
              "System Information", COL_SUBTEXT, COL_SURFACE0);
    fb_fill_rect(wx, wy + hdr_h, ww, 1, COL_SURFACE1);     /* divider     */

    int y = wy + hdr_h + 10;
    int x = wx + 14;
    int row_w = ww - 28;

    /* ── Section heading ── */
    font_puts(x, y, "SYSTEM", COL_OVERLAY0, COL_MANTLE);
    fb_fill_rect(x + 56, y + 6, row_w - 56, 1, COL_SURFACE1);
    y += 22;

    /* Stat row helper: label + value on COL_SURFACE0 pill */
#define STAT_ROW(lbl, val, val_col) \
    fb_fill_rounded_rect(x, y, row_w, 24, 4, COL_SURFACE0); \
    font_puts(x + 10, y + 4, lbl, COL_SUBTEXT, COL_SURFACE0); \
    font_puts(x + 118, y + 4, val, val_col, COL_SURFACE0); \
    y += 28

    STAT_ROW("Version",  "0.1 Genesis",    COL_TEAL);
    STAT_ROW("Arch",     "x86_64",         COL_BLUE);
    STAT_ROW("CPU",      "x86_64 (qemu)",  COL_BLUE);
    STAT_ROW("Kernel",   "NexOS Custom",   COL_LAVENDER);

#undef STAT_ROW

    /* Uptime badge (green-accented) */
    char upbuf[32]; get_uptime_str(upbuf);
    fb_fill_rounded_rect(x, y, row_w, 28, 6, COL_SURFACE0);
    fb_fill_rounded_rect(x, y, 4, 28, 4, COL_GREEN);
    font_puts(x + 12, y + 6, "Uptime", COL_SUBTEXT, COL_SURFACE0);
    font_puts(x + 118, y + 6, upbuf, COL_GREEN, COL_SURFACE0);
    y += 36;

    /* ── Memory section ── */
    font_puts(x, y, "MEMORY", COL_OVERLAY0, COL_MANTLE);
    fb_fill_rect(x + 60, y + 6, row_w - 60, 1, COL_SURFACE1);
    y += 22;

    uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint64_t free_mb  = pmm_get_free_memory()  / (1024 * 1024);
    int      used_mb  = (int)(total_mb > free_mb ? total_mb - free_mb : 0);
    int      pct      = (total_mb > 0) ? (used_mb * 100 / (int)total_mb) : 0;

    char rambuf[16], freebuf[16], usedbuf[16], pctbuf[8];
    fmt_mb((uint64_t)used_mb, usedbuf);
    fmt_mb(total_mb, rambuf);
    fmt_mb(free_mb, freebuf);
    fmt_pct(pct, pctbuf);

    /* Memory bar */
    int bar_w  = row_w;
    int fill_w = (total_mb > 0)
                 ? (int)((uint64_t)used_mb * (uint64_t)bar_w / total_mb)
                 : 0;
    uint32_t bar_col = (pct > 85) ? COL_RED
                     : (pct > 60) ? COL_PEACH
                                  : COL_BLUE;

    fb_fill_rounded_rect(x, y, bar_w, 22, 6, COL_SURFACE0);
    if (fill_w > 4) {
        fb_fill_rounded_rect(x, y, fill_w, 22, 6, bar_col);
        fb_fill_rect_blend(x, y, fill_w, 10, 0xFFFFFF, 18);   /* specular  */
    }
    /* Pct label to the right of the bar */
    y += 26;
    font_puts(x, y, "Used:", COL_SUBTEXT, COL_MANTLE);
    font_puts(x + 48, y, usedbuf, bar_col, COL_MANTLE);
    font_puts(x + 48 + (int)(slen(usedbuf) * 8) + 16, y,
              pctbuf, COL_SUBTEXT, COL_MANTLE);
    font_puts(x + row_w - 56, y, "Free:", COL_SUBTEXT, COL_MANTLE);
    font_puts(x + row_w - 56 + 48, y, freebuf, COL_GREEN, COL_MANTLE);
    y += 26;

    /* Three stat chips: Total / Used / Free */
    int chip_w = (row_w - 8) / 3;
    int chip_cols[3] = { 0, chip_w + 4, (chip_w + 4) * 2 };
    const char *chip_lbl[3] = { "Total", "Used", "Free" };
    const char *chip_val[3] = { rambuf, usedbuf, freebuf };
    uint32_t    chip_col[3] = { COL_TEXT, bar_col, COL_GREEN };
    for (int ci = 0; ci < 3; ci++) {
        int cx = x + chip_cols[ci];
        fb_fill_rounded_rect(cx, y, chip_w, 30, 6, COL_SURFACE0);
        font_puts(cx + 8, y + 4,  chip_lbl[ci], COL_SUBTEXT, COL_SURFACE0);
        font_puts(cx + 8, y + 16, chip_val[ci], chip_col[ci], COL_SURFACE0);
    }
    y += 40;

    /* ── Refresh button ── */
    fb_fill_rounded_rect(x, y, 112, 30, 8, COL_SURFACE1);
    fb_draw_rect_outline(x, y, 112, 30, COL_SURFACE2, 1);
    fb_fill_rect_blend(x, y, 112, 14, 0xFFFFFF, 8);   /* button specular */
    font_puts(x + 22, y + 7, "Refresh", COL_TEXT, COL_SURFACE1);
}

static void sysinfo_close(window_t *win) { wm_close(win); }

window_t *sysinfo_create(int x, int y) {
    window_t *win = wm_new(x, y, 380, 440, "System Information");
    if (!win) return NULL;
    win->on_paint = sysinfo_paint;
    win->on_close = sysinfo_close;
    return win;
}
