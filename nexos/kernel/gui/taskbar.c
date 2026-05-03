/* NexOS — kernel/gui/taskbar.c
 * Bottom taskbar with smooth hover-glow animations on all interactive elements.
 * MIT License */
#include "taskbar.h"
#include "anim.h"
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

#define TB_BTN_W  80    /* "Apps" button width */
#define TB_WIN_W  120   /* window pill width */
#define TB_WIN_STEP 128 /* stride between window pills */

static int tb_y;

/* ── Hover animation state ───────────────────────────────────────────────── */
static int tb_hover_mx = -1, tb_hover_my = -1;   /* last known mouse pos */
static int tb_apps_glow  = 0;                     /* 0-256 */
static int tb_win_glow[WM_MAX_WINDOWS];            /* per-window 0-256 */

void taskbar_init(void) {
    tb_y = (int)fb.height - TB_H;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) tb_win_glow[i] = 0;
    klog(LOG_INFO, "Taskbar: initialized");
}

int taskbar_get_y(void) { return tb_y; }

void taskbar_handle_mouse(int mx, int my) {
    tb_hover_mx = mx;
    tb_hover_my = my;
}

static void itoa_u(uint32_t v, char *buf, int w) {
    char t[12]; int ti = 0;
    if (v == 0) { t[ti++] = '0'; }
    while (v) { t[ti++] = '0' + (int)(v % 10); v /= 10; }
    int bi = 0;
    while (bi < w - ti) buf[bi++] = ' ';
    while (ti > 0) buf[bi++] = t[--ti];
    buf[bi] = 0;
}

static void draw_wifi_bars(int x, int y, int connected, int signal) {
    if (!connected) { font_puts(x, y, "~", 0x585B70, 0x1A1A2E); return; }
    uint32_t c_hi = signal > 70 ? 0xA6E3A1 :
                    signal > 40 ? 0xF9E2AF : 0xF38BA8;
    int heights[3] = { 4, 8, 12 };
    for (int i = 0; i < 3; i++) {
        uint32_t bc = (i == 2)               ? c_hi :
                      (i == 1 && signal >= 40) ? c_hi :
                      (i == 0 && signal >= 20) ? c_hi : 0x45475A;
        fb_fill_rect(x + i*5, y + (12-heights[i]), 4, heights[i], bc);
    }
}

void taskbar_draw(void) {
    if (!fb.initialized) return;
    tb_y = (int)fb.height - TB_H;

    /* ── Animate hover glow (advance every draw call ~30fps) ── */
    int over_apps = (tb_hover_my >= tb_y &&
                     tb_hover_mx >= 8 && tb_hover_mx < 8 + TB_BTN_W);
    if (over_apps) tb_apps_glow = anim_clamp(tb_apps_glow + 18, 0, 256);
    else           tb_apps_glow = anim_clamp(tb_apps_glow - 14, 0, 256);

    int wc = wm_window_count();
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        int bx    = 100 + i * TB_WIN_STEP;
        int over  = (i < wc) && (tb_hover_my >= tb_y) &&
                    (tb_hover_mx >= bx && tb_hover_mx < bx + TB_WIN_W);
        if (over) tb_win_glow[i] = anim_clamp(tb_win_glow[i] + 18, 0, 256);
        else      tb_win_glow[i] = anim_clamp(tb_win_glow[i] - 14, 0, 256);
    }

    /* ── Background bar ── */
    fb_fill_rect(0, tb_y, (int)fb.width, TB_H, 0x1A1A2E);
    fb_fill_rect(0, tb_y,     (int)fb.width, 1, 0x4A4B7A);
    fb_fill_rect(0, tb_y + 1, (int)fb.width, 1, 0x252645);

    /* ── Apps button with hover glow ── */
    uint32_t apps_bg  = anim_color_lerp(0x252645, 0x3A3B72, tb_apps_glow);
    uint32_t apps_rim = anim_color_lerp(0x4A4B7A, 0x8888CC, tb_apps_glow);
    fb_fill_rounded_rect(8, tb_y + 6, TB_BTN_W, 28, 8, apps_bg);
    fb_draw_rect_outline(8, tb_y + 6, TB_BTN_W, 28, apps_rim, 1);
    /* Glow under-line */
    if (tb_apps_glow > 20)
        fb_fill_rect_blend(8, tb_y + 33, TB_BTN_W, 2, COL_BLUE,
                           (uint8_t)(tb_apps_glow * 180 / 256));
    font_puts(18, tb_y + 13, "Apps", COL_BLUE, apps_bg);

    /* ── Window list pills ── */
    int bx = 100;
    for (int i = 0; i < wc; i++) {
        window_t *win = wm_get_window(i);
        if (!win || win->state == WIN_MINIMIZED) continue;
        if (bx > (int)fb.width - 280) break;

        uint32_t base_col = win->focused ? 0x3A3B60 : 0x252645;
        uint32_t hov_col  = win->focused ? 0x4A4C80 : 0x343568;
        uint32_t btn_col  = anim_color_lerp(base_col, hov_col, tb_win_glow[i]);

        fb_fill_rounded_rect(bx, tb_y + 6, TB_WIN_W, 28, 6, btn_col);
        fb_draw_rect_outline(bx, tb_y + 6, TB_WIN_W, 28, 0x4A4B7A, 1);

        /* Active indicator line */
        if (win->focused)
            fb_fill_rect(bx + 6, tb_y + 32, TB_WIN_W - 12, 2, COL_BLUE);
        else if (tb_win_glow[i] > 10)
            fb_fill_rect_blend(bx + 6, tb_y + 32, TB_WIN_W - 12, 2,
                               COL_SURFACE2, (uint8_t)(tb_win_glow[i] * 120 / 256));

        /* Title (truncated) */
        char short_title[15]; int k = 0;
        while (k < 14 && win->title[k]) { short_title[k] = win->title[k]; k++; }
        short_title[k] = 0;
        font_puts(bx + 8, tb_y + 13, short_title, COL_TEXT, btn_col);
        bx += TB_WIN_STEP;
    }

    /* ── Right tray ── */
    int rx = (int)fb.width - 8;

    /* Clock */
    rtc_time_t t; rtc_get_time(&t);
    char clock_str[6];
    clock_str[0] = '0' + t.hour   / 10; clock_str[1] = '0' + t.hour   % 10;
    clock_str[2] = ':';
    clock_str[3] = '0' + t.minute / 10; clock_str[4] = '0' + t.minute % 10;
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

    /* WiFi */
    rx -= 100;
    if (wifi_is_connected()) {
        int sig = wifi_get_signal();
        draw_wifi_bars(rx, tb_y + 14, 1, sig);
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
    if (x >= 8 && x < 8 + TB_BTN_W) {
        if (launcher_is_visible()) launcher_hide();
        else launcher_show(8, tb_y);
        return;
    }
    int bx = 100;
    int wc = wm_window_count();
    for (int i = 0; i < wc; i++) {
        window_t *win = wm_get_window(i);
        if (!win) continue;
        if (x >= bx && x < bx + TB_WIN_W) {
            if (win->state == WIN_MINIMIZED) {
                win->state = WIN_NORMAL;
                wm_focus(win); wm_raise(win);
            } else if (win->focused) {
                wm_minimize(win);
            } else {
                wm_focus(win); wm_raise(win);
            }
            return;
        }
        bx += TB_WIN_STEP;
        if (bx > (int)fb.width - 280) break;
    }
}

void taskbar_update(void) { /* called every second — draw handles the update */ }
