/* NexOS — kernel/gui/clock_app.c | GUI Clock | MIT License */
#include "clock_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/rtc.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void u8_2d(uint8_t v, char *buf) {
    buf[0] = '0' + v / 10;
    buf[1] = '0' + v % 10;
    buf[2] = 0;
}
static void u16_4d(uint16_t v, char *buf) {
    buf[0]='0'+v/1000; buf[1]='0'+(v/100)%10;
    buf[2]='0'+(v/10)%10; buf[3]='0'+v%10; buf[4]=0;
}

/* Day-of-week via Zeller's congruence (0=Sun … 6=Sat) */
static int day_of_week(int d, int m, int y) {
    if (m < 3) { m += 12; y--; }
    int k=y%100, j=y/100;
    int h=(d + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    /* Zeller: 0=Sat,1=Sun,2=Mon … convert → 0=Sun */
    return ((h + 6) % 7);
}

static const char *day_names[7]   = { "Sunday","Monday","Tuesday",
                                       "Wednesday","Thursday","Friday","Saturday" };
static const char *month_names[12] = { "January","February","March","April",
                                        "May","June","July","August",
                                        "September","October","November","December" };

/* ── Digit drawing with drop shadow (big 2x font) ────────────────────────── */
static void big_char(int x, int y, char c, uint32_t col, uint32_t bg) {
    char s[2]={c,0};
    /* Drop-shadow */
    font_puts2x(x+2, y+2, s, bg, bg);
    font_puts2x(x,   y,   s, col, bg);
}

static void big_str(int x, int y, const char *s, uint32_t col, uint32_t bg) {
    while (*s) {
        big_char(x, y, *s, col, bg);
        x += 16;
        s++;
    }
}

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void clock_paint(window_t *win) {
    clock_app_t *c = (clock_app_t *)win->userdata;
    if (!c) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;

    fb_fill_rect(wx, wy, ww, win->h - WM_TITLEBAR_H, COL_BASE);

    rtc_time_t t;
    rtc_get_time(&t);

    /* ── Time display (HH:MM:SS) ── */
    /* Build string "HH:MM:SS" */
    char hbuf[3], mbuf[3], sbuf[3];
    u8_2d(t.hour,   hbuf);
    u8_2d(t.minute, mbuf);
    u8_2d(t.second, sbuf);

    /* Char width at 2x: 16px per char, total = 8 chars + 2 colons = 8*16 = 128 */
    int time_w = 8 * 16;  /* "HH:MM:SS" = 8 chars at 16px each */
    int tx = wx + (ww - time_w) / 2;
    int ty = wy + 22;

    /* Background pill for clock */
    fb_fill_rounded_rect(tx - 12, ty - 6, time_w + 24, 44, 10, COL_SURFACE0);
    fb_fill_rect_blend(tx - 12, ty - 6, time_w + 24, 20, 0xFFFFFF, 8);

    /* Hours */
    big_str(tx,      ty, hbuf, COL_TEXT, COL_SURFACE0);
    /* Colon — blink every second */
    uint32_t colon_col = (t.second & 1) ? COL_SURFACE2 : COL_SUBTEXT;
    big_char(tx + 32, ty, ':', colon_col, COL_SURFACE0);
    /* Minutes */
    big_str(tx + 48,  ty, mbuf, COL_TEXT, COL_SURFACE0);
    /* Colon */
    big_char(tx + 80, ty, ':', colon_col, COL_SURFACE0);
    /* Seconds */
    big_str(tx + 96,  ty, sbuf, COL_SUBTEXT, COL_SURFACE0);

    /* ── Date line ── */
    int dow = day_of_week(t.day, t.month, (int)t.year);
    const char *dow_s   = (dow >= 0 && dow < 7)  ? day_names[dow] : "---";
    const char *mon_s   = (t.month>=1&&t.month<=12)
                          ? month_names[t.month-1] : "---";

    /* Build: "Wednesday, 3 May 2026" */
    char date[48]; int di=0;
    const char *p=dow_s; while(*p) date[di++]=*p++;
    date[di++]=','; date[di++]=' ';
    /* day */
    if (t.day >= 10) date[di++]='0'+t.day/10;
    date[di++]='0'+t.day%10;
    date[di++]=' ';
    p=mon_s; while(*p) date[di++]=*p++;
    date[di++]=' ';
    char yearbuf[5]; u16_4d(t.year, yearbuf);
    p=yearbuf; while(*p) date[di++]=*p++;
    date[di]=0;

    int date_w = di * 8;
    int dx = wx + (ww - date_w) / 2;
    font_puts(dx, wy + 76, date, COL_SUBTEXT, COL_BASE);

    /* ── Accent rule ── */
    int rul_y = wy + 96;
    fb_fill_rect(wx + 24, rul_y, ww - 48, 1, COL_SURFACE1);

    /* ── Seconds progress bar ── */
    int bar_x = wx + 24, bar_y = rul_y + 10;
    int bar_w = ww - 48;
    fb_fill_rounded_rect(bar_x, bar_y, bar_w, 8, 4, COL_SURFACE0);
    int fill = bar_w * t.second / 59;
    if (fill > 0) {
        uint32_t bc = (t.second >= 45) ? COL_PEACH
                    : (t.second >= 30) ? COL_YELLOW
                                       : COL_BLUE;
        fb_fill_rounded_rect(bar_x, bar_y, fill, 8, 4, bc);
        fb_fill_rect_blend(bar_x, bar_y, fill, 4, 0xFFFFFF, 20);
    }

    /* ── 12-hour AM/PM indicator ── */
    const char *ampm = (t.hour < 12) ? "AM" : "PM";
    uint8_t h12 = t.hour % 12; if (!h12) h12 = 12;
    char h12s[3]; u8_2d(h12, h12s);
    /* remove leading zero from 12h */
    const char *h12_disp = (h12s[0]=='0') ? h12s+1 : h12s;

    char ampm_str[10]; int ai=0;
    p=h12_disp; while(*p) ampm_str[ai++]=*p++;
    ampm_str[ai++]=' ';
    p=ampm; while(*p) ampm_str[ai++]=*p++;
    ampm_str[ai]=0;

    int ap_w = ai * 8;
    fb_fill_rounded_rect(wx + ww/2 - ap_w/2 - 8, rul_y + 28,
                         ap_w + 16, 22, 6, COL_SURFACE0);
    font_puts(wx + ww/2 - ap_w/2, rul_y + 34,
              ampm_str, COL_TEAL, COL_SURFACE0);

    /* Schedule repaint every second by always invalidating */
    if (t.second != c->last_sec) {
        c->last_sec = t.second;
        wm_invalidate(win);
    }
}

static void clock_close(window_t *win) {
    clock_app_t *c = (clock_app_t *)win->userdata;
    if (c) { kfree(c); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
clock_app_t *clock_create(int x, int y) {
    window_t *win = wm_new(x, y, 300, 176, "Clock");
    if (!win) return NULL;

    clock_app_t *c = (clock_app_t *)kmalloc(sizeof(clock_app_t));
    if (!c) { wm_close(win); return NULL; }
    for (int i=0; i<(int)sizeof(clock_app_t); i++) ((uint8_t*)c)[i]=0;

    c->win      = win;
    c->last_sec = 255;   /* force first repaint */

    win->on_paint = clock_paint;
    win->on_close = clock_close;
    win->userdata = c;

    /* Clock needs continuous repainting; prime the pump */
    wm_invalidate(win);
    return c;
}
