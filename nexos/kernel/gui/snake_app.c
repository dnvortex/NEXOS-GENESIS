/* NexOS — kernel/gui/snake_app.c | Snake Game | MIT License */
#include "snake_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

static void snzero(void *p, int n){uint8_t *b=(uint8_t*)p;for(int i=0;i<n;i++)b[i]=0;}
static int snmax(int a,int b){return a>b?a:b;}

/* ── RNG ─────────────────────────────────────────────────────────────────── */
static int sn_rand(snake_app_t *g, int max) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return (int)((g->rng >> 16) % (uint32_t)max);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void int_str(int v, char *buf) {
    char t[12]; int ti=0;
    if (!v){t[ti++]='0';}else while(v>0){t[ti++]='0'+v%10;v/=10;}
    int bi=0; while(ti>0) buf[bi++]=t[--ti]; buf[bi]=0;
}
static int slen(const char *s){int n=0;while(s[n])n++;return n;}

/* ── Food placement ──────────────────────────────────────────────────────── */
static void place_food(snake_app_t *g) {
    int tries = 0;
    do {
        g->food_x = sn_rand(g, SN_W);
        g->food_y = sn_rand(g, SN_H);
        tries++;
    } while (g->grid[g->food_y][g->food_x] && tries < 2000);
}

/* ── Init / reset ────────────────────────────────────────────────────────── */
static void snake_reset(snake_app_t *g) {
    snzero(g->bx,   sizeof(g->bx));
    snzero(g->by,   sizeof(g->by));
    snzero(g->grid, sizeof(g->grid));
    g->head      = 2;
    g->body_len  = 3;
    g->dx        = 1;  g->dy = 0;
    g->next_dx   = 1;  g->next_dy = 0;
    g->score     = 0;
    g->game_over = 0;
    g->paused    = 0;
    g->speed_ms  = 200;

    /* Place snake horizontally at centre */
    int sy = SN_H / 2;
    g->bx[0] = (uint8_t)(SN_W/2 - 2); g->by[0] = (uint8_t)sy;
    g->bx[1] = (uint8_t)(SN_W/2 - 1); g->by[1] = (uint8_t)sy;
    g->bx[2] = (uint8_t)(SN_W/2);     g->by[2] = (uint8_t)sy;
    g->grid[sy][SN_W/2-2] = 1;
    g->grid[sy][SN_W/2-1] = 1;
    g->grid[sy][SN_W/2  ] = 1;

    place_food(g);
    g->last_move = timer_get_ticks();
}

/* ── One game step ───────────────────────────────────────────────────────── */
static void snake_step(snake_app_t *g) {
    /* Apply buffered direction (prevent 180° reversal) */
    int can_x = !(g->next_dx == -g->dx && g->body_len > 1);
    int can_y = !(g->next_dy == -g->dy && g->body_len > 1);
    if (g->next_dx != 0 || g->next_dy != 0) {
        if (can_x && can_y) { g->dx = g->next_dx; g->dy = g->next_dy; }
    }

    int nx = (int)g->bx[g->head] + g->dx;
    int ny = (int)g->by[g->head] + g->dy;

    /* Wall collision */
    if (nx < 0 || nx >= SN_W || ny < 0 || ny >= SN_H) { g->game_over = 1; return; }

    int eating = (nx == g->food_x && ny == g->food_y);

    /* Remove tail from grid FIRST (unless growing) */
    if (!eating) {
        int tail = (g->head - g->body_len + 1 + SN_MAX_LEN) % SN_MAX_LEN;
        g->grid[g->by[tail]][g->bx[tail]] = 0;
    }

    /* Self-collision */
    if (g->grid[ny][nx]) { g->game_over = 1; return; }

    /* Advance head */
    g->head = (g->head + 1) % SN_MAX_LEN;
    g->bx[g->head] = (uint8_t)nx;
    g->by[g->head] = (uint8_t)ny;
    g->grid[ny][nx] = 1;

    if (eating) {
        g->body_len++;
        g->score++;
        if (g->score > g->best) g->best = g->score;
        /* Speed up (min 80ms per step) */
        g->speed_ms = (uint32_t)snmax(80, 200 - g->score * 4);
        place_food(g);
    }
}

/* ── Draw ────────────────────────────────────────────────────────────────── */
#define GRID_X    1
#define BORDER_C  COL_SURFACE1

static void snake_paint(window_t *win) {
    snake_app_t *g = (snake_app_t *)win->userdata;
    if (!g) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;

    /* Advance game if enough time has passed */
    if (!g->game_over && !g->paused) {
        uint64_t now = timer_get_ticks();
        if (now - g->last_move >= g->speed_ms) {
            snake_step(g);
            g->last_move = now;
        }
        wm_invalidate(win);  /* keep the game loop running */
    }

    /* ── Header bar ── */
    fb_fill_rect(wx, wy, SN_W*SN_CELL+2, SN_HEADER_H, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, SN_W*SN_CELL+2, 1, 0xFFFFFF, 12);
    font_puts2x(wx+6, wy+4, "SNAKE", COL_GREEN, COL_SURFACE0);

    /* Score */
    char sc[20]; int_str(g->score, sc); int sl=slen(sc);
    char sb[32]; int sbi=0;
    const char *sp="Score:"; while(*sp) sb[sbi++]=*sp++;
    const char *p=sc; while(*p) sb[sbi++]=*p++; sb[sbi]=0;
    font_puts(wx+90, wy+9, sb, COL_YELLOW, COL_SURFACE0);

    /* Best */
    char bs[20]; int_str(g->best, bs);
    char bb[24]; int bpi=0;
    const char *bp="Best:"; while(*bp) bb[bpi++]=*bp++;
    p=bs; while(*p) bb[bpi++]=*p++; bb[bpi]=0;
    font_puts(wx+90+sl*8+60, wy+9, bb, COL_TEAL, COL_SURFACE0);

    /* Speed indicator */
    int spd_bars = (200 - (int)g->speed_ms) / 24 + 1;
    for (int i = 0; i < 6; i++) {
        uint32_t bc = (i < spd_bars) ? COL_GREEN : COL_SURFACE1;
        fb_fill_rounded_rect(wx + SN_W*SN_CELL - 60 + i*10, wy+10, 8, 14, 2, bc);
    }
    font_puts(wx + SN_W*SN_CELL - 68, wy+9, "SPD", COL_OVERLAY0, COL_SURFACE0);

    /* Pause indicator */
    if (g->paused)
        font_puts(wx+SN_W*SN_CELL/2-12, wy+9, "PAUSED", COL_YELLOW, COL_SURFACE0);

    /* ── Grid border ── */
    int gy = wy + SN_HEADER_H;
    fb_fill_rect(wx, gy, SN_W*SN_CELL+2, SN_H*SN_CELL, COL_BASE);
    fb_draw_rect_outline(wx, gy, SN_W*SN_CELL+2, SN_H*SN_CELL, BORDER_C, 1);

    /* ── Draw food ── */
    int fx = wx + GRID_X + g->food_x * SN_CELL + 2;
    int fy = gy + g->food_y * SN_CELL + 2;
    fb_fill_rounded_rect(fx, fy, SN_CELL-4, SN_CELL-4, 4, COL_RED);
    fb_fill_rect_blend(fx+2, fy+2, SN_CELL-8, 4, 0xFFFFFF, 60);  /* specular */
    /* Outer glow */
    fb_fill_rect_blend(wx+GRID_X+g->food_x*SN_CELL+1, gy+g->food_y*SN_CELL+1,
                       SN_CELL, SN_CELL, COL_RED, 20);

    /* ── Draw snake ── */
    /* Body cells from grid (fast O(WxH) scan) */
    for (int cy = 0; cy < SN_H; cy++) {
        for (int cx = 0; cx < SN_W; cx++) {
            if (!g->grid[cy][cx]) continue;
            int px = wx + GRID_X + cx * SN_CELL + 1;
            int py = gy + cy * SN_CELL + 1;
            fb_fill_rounded_rect(px, py, SN_CELL-2, SN_CELL-2, 3, COL_GREEN);
            fb_fill_rect_blend(px+1, py+1, SN_CELL-4, 3, 0xFFFFFF, 40);
        }
    }

    /* Head — slightly different colour + eyes */
    if (!g->game_over) {
        int hpx = wx + GRID_X + (int)g->bx[g->head] * SN_CELL + 1;
        int hpy = gy + (int)g->by[g->head] * SN_CELL + 1;
        fb_fill_rounded_rect(hpx, hpy, SN_CELL-2, SN_CELL-2, 3, COL_TEAL);
        fb_fill_rect_blend(hpx+1, hpy+1, SN_CELL-4, 3, 0xFFFFFF, 60);
        /* Eyes */
        fb_fill_circle(hpx + SN_CELL/2 - 2 + g->dx,
                       hpy + SN_CELL/2 - 2 + g->dy, 2, 0x0D0D1A);
    }

    /* ── Game-over overlay ── */
    if (g->game_over) {
        fb_fill_rect_blend(wx, gy, SN_W*SN_CELL+2, SN_H*SN_CELL, 0x0A0A14, 140);

        int ow = 200, oh = 80;
        int ox = wx + (SN_W*SN_CELL+2 - ow) / 2;
        int oy = gy + (SN_H*SN_CELL - oh) / 2;
        fb_fill_rounded_rect(ox, oy, ow, oh, 12, COL_SURFACE0);
        fb_draw_rect_outline(ox, oy, ow, oh, COL_RED, 2);
        fb_fill_rect(ox, oy, 4, oh, COL_RED);

        font_puts2x(ox + 20, oy + 10, "GAME", COL_RED, COL_SURFACE0);
        font_puts2x(ox + 20, oy + 30, "OVER", COL_RED, COL_SURFACE0);

        char fsc[24]; int fsi=0;
        const char *fl="Score:"; while(*fl) fsc[fsi++]=*fl++;
        int_str(g->score, sc); p=sc; while(*p) fsc[fsi++]=*p++;
        fsc[fsi]=0;
        font_puts(ox+20, oy+58, fsc, COL_SUBTEXT, COL_SURFACE0);
        font_puts(ox+20, oy+68, "Enter=Restart  P=Pause", COL_OVERLAY0, COL_SURFACE0);
    }
}

/* ── Input ───────────────────────────────────────────────────────────────── */
static void snake_key(window_t *win, char key) {
    snake_app_t *g = (snake_app_t *)win->userdata;
    if (!g) return;

    uint8_t uk = (uint8_t)key;

    if (g->game_over) {
        if (key == '\n' || key == '\r' || key == 'r' || key == 'R') {
            snake_reset(g);
            wm_invalidate(win);
        }
        return;
    }

    if (key == 'p' || key == 'P') {
        g->paused = !g->paused;
        if (!g->paused) g->last_move = timer_get_ticks();
        wm_invalidate(win);
        return;
    }
    if (g->paused) return;

    if      (uk == (uint8_t)KEY_UP    && g->dy != 1)  { g->next_dx=0;  g->next_dy=-1; }
    else if (uk == (uint8_t)KEY_DOWN  && g->dy != -1) { g->next_dx=0;  g->next_dy=1; }
    else if (uk == (uint8_t)KEY_LEFT  && g->dx != 1)  { g->next_dx=-1; g->next_dy=0; }
    else if (uk == (uint8_t)KEY_RIGHT && g->dx != -1) { g->next_dx=1;  g->next_dy=0; }
    /* WASD fallback */
    else if ((key=='w'||key=='W') && g->dy != 1)  { g->next_dx=0;  g->next_dy=-1; }
    else if ((key=='s'||key=='S') && g->dy != -1) { g->next_dx=0;  g->next_dy=1; }
    else if ((key=='a'||key=='A') && g->dx != 1)  { g->next_dx=-1; g->next_dy=0; }
    else if ((key=='d'||key=='D') && g->dx != -1) { g->next_dx=1;  g->next_dy=0; }
}

static void snake_close(window_t *win) {
    snake_app_t *g = (snake_app_t *)win->userdata;
    if (g) { kfree(g); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
snake_app_t *snake_create(int x, int y) {
    int ww = SN_W * SN_CELL + 2;
    int wh = SN_H * SN_CELL + SN_HEADER_H + WM_TITLEBAR_H;
    window_t *win = wm_new(x, y, ww, wh, "Snake");
    if (!win) return NULL;

    snake_app_t *g = (snake_app_t *)kmalloc(sizeof(snake_app_t));
    if (!g) { wm_close(win); return NULL; }
    snzero(g, sizeof(snake_app_t));

    g->win  = win;
    g->rng  = 0xDEADBEEF;
    g->best = 0;
    snake_reset(g);

    win->on_paint = snake_paint;
    win->on_key   = snake_key;
    win->on_close = snake_close;
    win->userdata = g;
    wm_invalidate(win);
    return g;
}
