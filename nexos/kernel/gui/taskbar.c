/* NexOS — kernel/gui/taskbar.c | Bottom taskbar | MIT License */
#include "taskbar.h"
#include "wm.h"
#include "launcher.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/rtc.h"
#include "../drivers/wifi.h"
#include "../net/netif.h"
#include "../mm/pmm.h"
#include "../kernel.h"
#include <stdint.h>

static int tb_y;

void taskbar_init(void) {
    tb_y = (int)fb.height - TB_H;
    klog(LOG_INFO, "Taskbar: initialized");
}

int taskbar_get_y(void) { return tb_y; }

static void itoa_u(uint32_t v, char *buf, int w) {
    char t[12]; int ti = 0;
    if (v == 0) { t[ti++] = '0'; }
    while (v) { t[ti++] = '0' + (int)(v % 10); v /= 10; }
    int bi = 0;
    while (bi < w - ti) buf[bi++] = ' ';
    while (ti > 0) buf[bi++] = t[--ti];
    buf[bi] = 0;
}

/* 3-bar WiFi signal icon (14×14 px) */
static void draw_wifi_bars(int x, int y, int connected, int signal) {
    if (!connected) {
        font_puts(x, y, "~", 0x585B70, 0x1A1A2E);
        return;
    }
    uint32_t c_hi = signal > 70 ? 0xA6E3A1 :
                    signal > 40 ? 0xF9E2AF : 0xF38BA8;
    /* bar 0 = leftmost/smallest, bar 2 = tallest */
    int heights[3] = { 4, 8, 12 };
    for (int i = 0; i < 3; i++) {
        uint32_t bc = (i == 2)                      ? c_hi :
                      (i == 1 && signal >= 40)      ? c_hi :
                      (i == 0 && signal >= 20)      ? c_hi : 0x45475A;
        int bh = heights[i];
        fb_fill_rect(x + i * 5, y + (12 - bh), 4, bh, bc);
    }
}

void taskbar_draw(void) {
    if (!fb.initialized) return;
    tb_y = (int)fb.height - TB_H;

    /* Dark glass background */
    fb_fill_rect(0, tb_y, (int)fb.width, TB_H, 0x1A1A2E);
    /* Top rim highlight */
    fb_fill_rect(0, tb_y,     (int)fb.width, 1, 0x4A4B7A);
    fb_fill_rect(0, tb_y + 1, (int)fb.width, 1, 0x252645);

    /* Apps button — glassy pill */
    fb_fill_rounded_rect(8, tb_y + 6, 80, 28, 8, 0x252645);
    fb_draw_rect_outline(8, tb_y + 6, 80, 28, 0x4A4B7A, 1);
    font_puts(18, tb_y + 13, "Apps", COL_BLUE, 0x252645);

    /* Window list buttons */
    int bx = 100;
    int wc = wm_window_count();
    for (int i = 0; i < wc; i++) {
        window_t *win = wm_get_window(i);
        if (!win || win->state == WIN_MINIMIZED) continue;
        uint32_t btn_col = win->focused ? 0x3A3B60 : 0x252645;
        fb_fill_rounded_rect(bx, tb_y + 6, 120, 28, 6, btn_col);
        fb_draw_rect_outline(bx, tb_y + 6, 120, 28, 0x4A4B7A, 1);
        if (win->focused)
            fb_fill_rect(bx + 6, tb_y + 32, 108, 2, COL_BLUE);
        char short_title[15];
        int k = 0;
        while (k < 14 && win->title[k]) { short_title[k] = win->title[k]; k++; }
        short_title[k] = 0;
        font_puts(bx + 8, tb_y + 13, short_title, COL_TEXT, btn_col);
        bx += 128;
        if (bx > (int)fb.width - 280) break;
    }

    /* ── Right-side tray ─────────────────────────────────────────────────── */
    int rx = (int)fb.width - 8;

    /* Clock */
    rtc_time_t t; rtc_get_time(&t);
    char clock_str[6];
    clock_str[0] = '0' + t.hour   / 10;
    clock_str[1] = '0' + t.hour   % 10;
    clock_str[2] = ':';
    clock_str[3] = '0' + t.minute / 10;
    clock_str[4] = '0' + t.minute % 10;
    clock_str[5] = 0;
    rx -= 48;
    font_puts(rx, tb_y + 13, clock_str, COL_TEXT, 0x1A1A2E);

    /* Memory */
    rx -= 8;
    uint32_t free_mb = (uint32_t)(pmm_get_free_frames() * 4 / 1024);
    char mem_str[8];
    itoa_u(free_mb, mem_str, 1);
    int mi = 0; while (mem_str[mi]) mi++;
    mem_str[mi++] = 'M'; mem_str[mi] = 0;
    rx -= (mi * 8 + 4);
    font_puts(rx, tb_y + 13, mem_str, COL_SUBTEXT, 0x1A1A2E);

    /* WiFi / network indicator */
    rx -= 100;
    if (wifi_is_connected()) {
        int sig = wifi_get_signal();
        draw_wifi_bars(rx, tb_y + 14, 1, sig);
        /* Short SSID label (up to 8 chars) */
        const char *ssid = wifi_get_ssid();
        char s8[9]; int si = 0;
        while (si < 8 && ssid[si]) { s8[si] = ssid[si]; si++; }
        s8[si] = 0;
        font_puts(rx + 18, tb_y + 13, s8, 0xA6E3A1, 0x1A1A2E);
    } else if (netif_is_up()) {
        fb_fill_rounded_rect(rx, tb_y + 9, 38, 22, 5, 0x252645);
        fb_draw_rect_outline(rx, tb_y + 9, 38, 22, 0x4A4B7A, 1);
        font_puts(rx + 4, tb_y + 14, "ETH", COL_BLUE, 0x252645);
    } else {
        draw_wifi_bars(rx, tb_y + 14, 0, 0);
        font_puts(rx + 18, tb_y + 13, "No net", 0x585B70, 0x1A1A2E);
    }
}

void taskbar_handle_click(int x, int y) {
    if (y < tb_y) return;
    /* Apps button */
    if (x >= 8 && x < 88) {
        if (launcher_is_visible()) launcher_hide();
        else launcher_show(8, tb_y);
        return;
    }
    /* Window list buttons */
    int bx = 100;
    int wc = wm_window_count();
    for (int i = 0; i < wc; i++) {
        window_t *win = wm_get_window(i);
        if (!win) continue;
        if (x >= bx && x < bx + 120) {
            if (win->state == WIN_MINIMIZED) {
                win->state = WIN_NORMAL;
                wm_focus(win);
                wm_raise(win);
            } else if (win->focused) {
                wm_minimize(win);
            } else {
                wm_focus(win);
                wm_raise(win);
            }
            return;
        }
        bx += 128;
        if (bx > (int)fb.width - 280) break;
    }
}

void taskbar_update(void) {
    /* called every second — taskbar_draw() handles the update */
}
