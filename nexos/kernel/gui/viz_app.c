/* NexOS — kernel/gui/viz_app.c | Music Visualizer | MIT License */
#include "viz_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

static void vzero(void *p, int n) { uint8_t *b=(uint8_t*)p; for(int i=0;i<n;i++) b[i]=0; }

/* ── Triangle wave oscillator (no FPU) ───────────────────────────────────── */
/* Returns 0..255 for a smooth cycle of given period (in frames) */
static int triwave(int t, int period, int phase) {
    if (period <= 0) return 128;
    int pos = ((t + phase) % period + period) % period;
    int half = period / 2;
    if (!half) return 0;
    if (pos < half) return pos * 255 / half;
    else            return 255 - (pos - half) * 255 / half;
}

/* Heat-map color: cold=blue, warm=cyan/green, hot=yellow/red */
static uint32_t heat_color(int v) {
    uint32_t r, g, b;
    if (v < 64)       { r=0;   g=0;          b=(uint32_t)(128+v); }
    else if (v < 128) { r=0;   g=(uint32_t)((v-64)*4); b=192; }
    else if (v < 192) { int d=v-128; r=(uint32_t)(d*4); g=255; b=(uint32_t)(192-d*3); }
    else              { int d=v-192; r=255; g=(uint32_t)(255-d*4); b=0; }
    return (r<<16)|(g<<8)|b;
}

/* Bar value: weighted sum of multiple oscillators, 0..255 */
static int bar_val(int bar, uint32_t frame) {
    int t = (int)frame;
    int v = (triwave(t, 20+bar,    bar*7)  * 3
           + triwave(t, 13+bar*2,  bar*13) * 2
           + triwave(t, 7+(35-bar),bar*19))  / 6;
    /* Add a global "bass beat" every ~60 frames, amplifies low bars */
    int beat_phase = (t % 60);
    if (beat_phase < 8 && bar < 8) {
        int boost = (8 - beat_phase) * 16 * (8 - bar) / 8;
        v = v + boost;
        if (v > 255) v = 255;
    }
    return v;
}

/* Waveform sample: sum of harmonics, 0..255 */
static int wave_sample(int x, uint32_t frame) {
    int t = (int)frame;
    int v = (triwave(t*3+x*2, 50,  0) * 3
           + triwave(t*7+x*5, 23, 80) * 2
           + triwave(t*11+x*8,11, 160)) / 6;
    return v;
}

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define WIN_W     640
#define WIN_H     360
#define WAVE_H    100
#define BAR_AREA  180

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void viz_paint(window_t *win) {
    viz_app_t *v = (viz_app_t *)win->userdata;
    if (!v) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;
    int ch = win->h - WM_TITLEBAR_H;
    (void)ww;

    v->frame++;

    fb_fill_rect(wx, wy, WIN_W, ch, COL_CRUST);

    /* ── Decorative header bar ── */
    fb_fill_rect(wx, wy, WIN_W, 28, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, WIN_W, 1, 0xFFFFFF, 10);
    font_puts2x(wx+12, wy+6, "VIZ", COL_MAUVE, COL_SURFACE0);
    font_puts(wx+46, wy+10, "Music Visualizer", COL_SUBTEXT, COL_SURFACE0);

    /* Frame counter as tiny readout */
    char fbuf[16]; int fi=0;
    uint32_t fr = v->frame % 10000;
    { char t[8]; int ti=0;
      if (!fr){t[ti++]='0';}else while(fr){t[ti++]='0'+fr%10;fr/=10;}
      while(ti>0) fbuf[fi++]=t[--ti]; fbuf[fi]=0; }
    font_puts(wx+WIN_W-fi*8-8, wy+10, fbuf, COL_OVERLAY0, COL_SURFACE0);

    /* ── Waveform area ── */
    int wave_y   = wy + 36;
    int wave_cy  = wave_y + WAVE_H / 2;

    /* Dark background */
    fb_fill_rect(wx, wave_y, WIN_W, WAVE_H, 0x0A0A14);

    /* Horizontal centerline */
    fb_fill_rect(wx, wave_cy, WIN_W, 1, COL_SURFACE1);

    /* Draw waveform as connected line segments */
    int prev_y = wave_cy;
    for (int x = 0; x < WIN_W; x++) {
        int sample = wave_sample(x, v->frame);
        int y = wave_cy - (sample - 128) * (WAVE_H/2 - 4) / 128;
        if (y < wave_y) y = wave_y;
        if (y >= wave_y + WAVE_H) y = wave_y + WAVE_H - 1;

        /* Glow line: 3 pixels wide with fading edges */
        fb_put_pixel(wx + x, y, COL_BLUE);
        if (y > wave_y)              fb_fill_rect_blend(wx+x, y-1, 1, 1, COL_BLUE, 80);
        if (y < wave_y + WAVE_H - 1) fb_fill_rect_blend(wx+x, y+1, 1, 1, COL_BLUE, 80);

        /* Fill between prev and current sample */
        if (x > 0) {
            int lo = prev_y < y ? prev_y : y;
            int hi = prev_y > y ? prev_y : y;
            for (int py = lo; py <= hi; py++)
                fb_put_pixel(wx + x, py, COL_BLUE);
        }
        prev_y = y;
    }

    /* ── Spectrum analyzer bars ── */
    int bar_base  = wave_y + WAVE_H + 8;
    int bar_total = WIN_W - 16;
    int bw        = bar_total / VIZ_BARS;
    if (bw < 2) bw = 2;
    int bar_gap   = bw > 4 ? 2 : 1;
    int binner    = bw - bar_gap;

    fb_fill_rect(wx, bar_base - 2, WIN_W, BAR_AREA + 6, 0x0A0A14);
    fb_fill_rect(wx, bar_base + BAR_AREA + 2, WIN_W, 1, COL_SURFACE0);

    for (int i = 0; i < VIZ_BARS; i++) {
        int val     = bar_val(i, v->frame);
        int target  = val * BAR_AREA / 255;

        /* Smooth bar heights */
        int cur  = v->bar_h[i];
        if (target > cur) cur = cur + (target - cur + 1) / 2;
        else              cur = cur - (cur - target + 3) / 4;
        if (cur < 0) cur = 0;
        v->bar_h[i] = cur;

        /* Peak hold */
        if (cur > v->peak[i])        { v->peak[i] = cur; v->peak_hold[i] = 20; }
        else if (v->peak_hold[i] > 0) { v->peak_hold[i]--; }
        else if (v->peak[i] > 0)      { v->peak[i]--; }

        if (!cur) continue;

        int bx0 = wx + 8 + i * bw;
        int by0 = bar_base + BAR_AREA - cur;

        /* Draw bar with gradient (dark at base, bright at top) */
        for (int row = 0; row < cur; row++) {
            int bright = 128 + row * 127 / (cur ? cur : 1);
            int rv = val * bright / 255;
            uint32_t col = heat_color(rv);
            fb_fill_rect(bx0, bar_base + BAR_AREA - 1 - row, binner, 1, col);
        }

        /* Peak dot */
        if (v->peak[i] > 2) {
            uint32_t pc = heat_color(val);
            fb_fill_rect(bx0, bar_base + BAR_AREA - v->peak[i], binner, 2, pc);
            fb_fill_rect_blend(bx0, bar_base + BAR_AREA - v->peak[i], binner, 2, 0xFFFFFF, 60);
        }
    }

    /* ── Frequency labels ── */
    int lbl_y = bar_base + BAR_AREA + 5;
    const char *lbls[] = { "SUB", "BASS", "MID", "PRES", "BRIL", "AIR" };
    int lbl_count = 6;
    for (int i = 0; i < lbl_count; i++) {
        int lx = wx + 8 + (i * VIZ_BARS / lbl_count) * bw;
        font_puts(lx, lbl_y, lbls[i], COL_OVERLAY0, 0x0A0A14);
    }

    /* Keep animating */
    wm_invalidate(win);
}

static void viz_close(window_t *win) {
    viz_app_t *v = (viz_app_t *)win->userdata;
    if (v) { kfree(v); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
viz_app_t *viz_create(int x, int y) {
    int wh = WIN_H;
    window_t *win = wm_new(x, y, WIN_W, wh, "Music Visualizer");
    if (!win) return NULL;

    viz_app_t *v = (viz_app_t *)kmalloc(sizeof(viz_app_t));
    if (!v) { wm_close(win); return NULL; }
    vzero(v, sizeof(viz_app_t));

    v->win = win;
    win->on_paint = viz_paint;
    win->on_close = viz_close;
    win->userdata = v;
    wm_invalidate(win);
    return v;
}
