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

static void u64tostr(uint64_t v, char *buf) {
    char t[24]; int ti = 0;
    if (v == 0) { t[ti++] = '0'; }
    while (v) { t[ti++] = '0' + (int)(v % 10); v /= 10; }
    int bi = 0; while (ti > 0) buf[bi++] = t[--ti]; buf[bi] = 0;
}

static void get_uptime_str(char *buf) {
    uint64_t s = timer_get_uptime_seconds();
    uint64_t h = s / 3600; s %= 3600;
    uint64_t m = s / 60;   s %= 60;
    char tmp[8]; int bi = 0;
    u64tostr(h, tmp); int i = 0; while (tmp[i]) buf[bi++] = tmp[i++];
    buf[bi++] = 'h'; buf[bi++] = ' ';
    u64tostr(m, tmp); i = 0; while (tmp[i]) buf[bi++] = tmp[i++];
    buf[bi++] = 'm'; buf[bi++] = ' ';
    u64tostr(s, tmp); i = 0; while (tmp[i]) buf[bi++] = tmp[i++];
    buf[bi++] = 's'; buf[bi] = 0;
}

static void draw_info_row(int x, int y, const char *label, const char *value) {
    font_puts(x,       y, label, COL_SUBTEXT, COL_MANTLE);
    font_puts(x + 120, y, value, COL_TEXT,    COL_MANTLE);
}

static void sysinfo_paint(window_t *win) {
    int y = win->y + WM_TITLEBAR_H + 16;
    int x = win->x + 20;

    fb_fill_rect(win->x, win->y + WM_TITLEBAR_H,
                 win->w, win->h - WM_TITLEBAR_H, COL_MANTLE);

    font_puts2x(x, y, "NexOS", COL_BLUE, COL_MANTLE);
    y += 40;

    draw_info_row(x, y, "Version",  "0.1 Genesis");   y += 28;
    draw_info_row(x, y, "Arch",     "x86_64");         y += 28;
    draw_info_row(x, y, "Kernel",   "NexOS Custom");   y += 28;

    char upbuf[32]; get_uptime_str(upbuf);
    draw_info_row(x, y, "Uptime",   upbuf);            y += 28;

    uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint64_t free_mb  = pmm_get_free_memory()  / (1024 * 1024);
    char rambuf[16];
    u64tostr(total_mb, rambuf);
    int ri = 0; while (rambuf[ri]) ri++;
    rambuf[ri++] = 'M'; rambuf[ri++] = 'B'; rambuf[ri] = 0;
    draw_info_row(x, y, "RAM",      rambuf);           y += 28;

    char freebuf[16];
    u64tostr(free_mb, freebuf);
    ri = 0; while (freebuf[ri]) ri++;
    freebuf[ri++] = 'M'; freebuf[ri++] = 'B'; freebuf[ri] = 0;
    draw_info_row(x, y, "Free RAM", freebuf);          y += 28;

    draw_info_row(x, y, "CPU",      "x86_64 (emul)");  y += 28;

    y += 8;
    font_puts(x, y, "Memory usage:", COL_SUBTEXT, COL_MANTLE); y += 20;

    int bar_w = win->w - 40;
    int used_mb = (int)(total_mb - free_mb);
    int fill_w = (total_mb > 0) ? (int)((uint64_t)used_mb * (uint64_t)bar_w / total_mb) : 0;
    fb_fill_rounded_rect(x, y, bar_w, 16, 4, COL_SURFACE1);
    if (fill_w > 0)
        fb_fill_rounded_rect(x, y, fill_w, 16, 4, COL_BLUE);
    y += 28;

    fb_fill_rounded_rect(x, y, 100, 28, 6, COL_SURFACE1);
    font_puts(x + 20, y + 6, "Refresh", COL_TEXT, COL_SURFACE1);
}

static void sysinfo_close(window_t *win) { wm_close(win); }

window_t *sysinfo_create(int x, int y) {
    window_t *win = wm_new(x, y, 380, 420, "System Information");
    if (!win) return NULL;
    win->on_paint = sysinfo_paint;
    win->on_close = sysinfo_close;
    return win;
}
