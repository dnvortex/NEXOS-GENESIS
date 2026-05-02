/* NexOS — kernel/gui/notif.c | Toast notification system | MIT License */
#include "notif.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>

#define NOTIF_W  280
#define NOTIF_H   60
#define NOTIF_PAD 10

typedef struct {
    char     title[32];
    char     body[80];
    uint32_t ms_left;
    uint8_t  active;
} notif_t;

static notif_t notifs[NOTIF_MAX];

void notif_init(void) {
    for (int i = 0; i < NOTIF_MAX; i++) notifs[i].active = 0;
}

void notif_show(const char *title, const char *body, uint32_t ms) {
    /* find free slot */
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (!notifs[i].active) {
            int ti = 0; while (ti < 31 && title[ti]) { notifs[i].title[ti] = title[ti]; ti++; }
            notifs[i].title[ti] = 0;
            int bi = 0; while (bi < 79 && body[bi])  { notifs[i].body[bi]  = body[bi];  bi++; }
            notifs[i].body[bi] = 0;
            notifs[i].ms_left = ms;
            notifs[i].active  = 1;
            return;
        }
    }
    /* overwrite oldest */
    notifs[0] = notifs[1]; notifs[1] = notifs[2]; notifs[2] = notifs[3];
    int ti = 0; while (ti < 31 && title[ti]) { notifs[3].title[ti] = title[ti]; ti++; } notifs[3].title[ti] = 0;
    int bi = 0; while (bi < 79 && body[bi])  { notifs[3].body[bi]  = body[bi];  bi++; } notifs[3].body[bi]  = 0;
    notifs[3].ms_left = ms; notifs[3].active = 1;
}

void notif_tick(uint32_t delta_ms) {
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (!notifs[i].active) continue;
        if (notifs[i].ms_left <= delta_ms) notifs[i].active = 0;
        else notifs[i].ms_left -= delta_ms;
    }
}

void notif_draw(void) {
    if (!fb.initialized) return;
    int count = 0;
    for (int i = NOTIF_MAX - 1; i >= 0; i--) {
        if (!notifs[i].active) continue;
        int nx = (int)fb.width  - NOTIF_W - 12;
        int ny = (int)fb.height - 40 - (count + 1) * (NOTIF_H + 6);
        fb_fill_rounded_rect(nx, ny, NOTIF_W, NOTIF_H, 8, COL_SURFACE0);
        fb_draw_rect_outline(nx, ny, NOTIF_W, NOTIF_H, COL_SURFACE1, 1);
        /* colour accent on left */
        fb_fill_rounded_rect(nx, ny, 4, NOTIF_H, 4, COL_BLUE);
        font_puts(nx + NOTIF_PAD + 4, ny + 10, notifs[i].title, COL_BLUE, COL_SURFACE0);
        font_puts(nx + NOTIF_PAD + 4, ny + 30, notifs[i].body,  COL_TEXT, COL_SURFACE0);
        count++;
        if (count >= NOTIF_MAX) break;
    }
}
