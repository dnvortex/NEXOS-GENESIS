/* NexOS — kernel/gui/notif.c
 * Toast notifications with slide-in (ease-out-cubic) and fade-out animations.
 * MIT License */
#include "notif.h"
#include "anim.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>

#define NOTIF_W    280
#define NOTIF_H     60
#define NOTIF_PAD   10
#define NOTIF_GAP    6

#define SLIDE_MS   280   /* slide-in duration  (ms) */
#define FADE_MS    600   /* fade-out window at end of life (ms) */

typedef struct {
    char     title[32];
    char     body[80];
    uint32_t ms_left;
    uint32_t ms_total;
    int      slide_in;   /* 0 = off-screen right, 256 = parked */
    uint8_t  active;
} notif_t;

static notif_t notifs[NOTIF_MAX];

static void nstr_cpy(char *d, const char *s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}

void notif_init(void) {
    for (int i = 0; i < NOTIF_MAX; i++) notifs[i].active = 0;
}

void notif_show(const char *title, const char *body, uint32_t ms) {
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (!notifs[i].active) {
            nstr_cpy(notifs[i].title, title, 32);
            nstr_cpy(notifs[i].body,  body,  80);
            notifs[i].ms_left  = ms;
            notifs[i].ms_total = ms;
            notifs[i].slide_in = 0;
            notifs[i].active   = 1;
            return;
        }
    }
    /* evict oldest */
    for (int i = 0; i < NOTIF_MAX - 1; i++) notifs[i] = notifs[i + 1];
    int last = NOTIF_MAX - 1;
    nstr_cpy(notifs[last].title, title, 32);
    nstr_cpy(notifs[last].body,  body,  80);
    notifs[last].ms_left  = ms;
    notifs[last].ms_total = ms;
    notifs[last].slide_in = 0;
    notifs[last].active   = 1;
}

void notif_tick(uint32_t delta_ms) {
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (!notifs[i].active) continue;

        /* advance slide-in */
        if (notifs[i].slide_in < 256) {
            int step = (int)(delta_ms * 256 / SLIDE_MS);
            notifs[i].slide_in = anim_clamp(notifs[i].slide_in + step, 0, 256);
        }

        /* age out */
        if (notifs[i].ms_left <= delta_ms) notifs[i].active = 0;
        else notifs[i].ms_left -= delta_ms;
    }
}

void notif_draw(void) {
    if (!fb.initialized) return;

    int count = 0;
    for (int i = NOTIF_MAX - 1; i >= 0; i--) {
        if (!notifs[i].active) continue;

        /* ── Position ── */
        int nx_rest = (int)fb.width - NOTIF_W - 12;
        int ny      = (int)fb.height - 40 - (count + 1) * (NOTIF_H + NOTIF_GAP);

        /* Ease-out-cubic slide: notification decelerates into resting position */
        int eased = anim_ease_out_cubic(notifs[i].slide_in);
        int x_off = (NOTIF_W + 20) * (256 - eased) / 256;
        int nx    = nx_rest + x_off;

        /* ── Fade-out when near end of life ── */
        int fade = 256;
        if (notifs[i].slide_in >= 256 && notifs[i].ms_left < (uint32_t)FADE_MS)
            fade = (int)(notifs[i].ms_left * 256 / FADE_MS);
        uint8_t bg_alpha   = (uint8_t)(220 * fade / 256);
        uint8_t rim_alpha  = (uint8_t)(160 * fade / 256);
        uint8_t acc_alpha  = (uint8_t)(255 * fade / 256);

        if (bg_alpha < 4) { count++; continue; }

        /* ── Drop shadow ── */
        fb_fill_rect_blend(nx + 4, ny + 4, NOTIF_W, NOTIF_H,
                           0x000000, (uint8_t)(bg_alpha / 3));

        /* ── Card background ── */
        fb_fill_rect_blend(nx, ny, NOTIF_W, NOTIF_H, 0x1E1E2E, bg_alpha);

        /* ── Top rim specular ── */
        fb_fill_rect_blend(nx + 12, ny, NOTIF_W - 24, 1, 0x6C6F85, rim_alpha);

        /* ── Left accent bar ── */
        fb_fill_rect_blend(nx, ny + 6, 3, NOTIF_H - 12, COL_BLUE, acc_alpha);

        /* ── Progress underline — shows remaining life ── */
        if (notifs[i].ms_total > 0) {
            int bar_w = (int)((uint32_t)NOTIF_W * notifs[i].ms_left /
                              notifs[i].ms_total);
            if (bar_w > 0)
                fb_fill_rect_blend(nx, ny + NOTIF_H - 2, bar_w, 2,
                                   COL_BLUE, (uint8_t)(120 * fade / 256));
        }

        /* ── Text (fades toward background colour as notification expires) ── */
        if (fade > 12) {
            uint32_t c_title = anim_color_lerp(0x1E1E2E, COL_BLUE, fade);
            uint32_t c_body  = anim_color_lerp(0x1E1E2E, COL_TEXT, fade);
            font_puts(nx + NOTIF_PAD + 5, ny + 10, notifs[i].title,
                      c_title, 0x1E1E2E);
            font_puts(nx + NOTIF_PAD + 5, ny + 30, notifs[i].body,
                      c_body,  0x1E1E2E);
        }

        count++;
        if (count >= NOTIF_MAX) break;
    }
}
