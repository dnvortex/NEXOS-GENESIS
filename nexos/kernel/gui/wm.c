/* NexOS — kernel/gui/wm.c | Window Manager | MIT License */
#include "wm.h"
#include "anim.h"
#include "desktop.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../mm/heap.h"
#include "../kernel.h"
#include <stdint.h>
#include <stddef.h>

/* provided by heap.h */
void *kmalloc(size_t size);
void  kfree(void *ptr);

/* ── Window list (front = index 0) ──────────────────────────────────────── */
static window_t *wins[WM_MAX_WINDOWS];
static int       win_count = 0;
static int       next_id   = 1;
static window_t *focused_win = NULL;

/* ── Internal helpers ────────────────────────────────────────────────────── */
static void strncpy_s(char *d, const char *s, int n) {
    int i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}
static int point_in_circle(int px, int py, int cx, int cy, int r) {
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

static void wm_draw_window(window_t *win) {
    if (!win->visible || win->state == WIN_MINIMIZED) return;

    /* shadow */
    fb_fill_rounded_rect(win->x + WM_SHADOW_OFF, win->y + WM_SHADOW_OFF,
                         win->w, win->h, 8, WM_SHADOW_COL);
    /* border */
    fb_fill_rounded_rect(win->x - 1, win->y - 1,
                         win->w + 2, win->h + 2, 9, COL_SURFACE1);
    /* titlebar */
    fb_fill_rounded_rect(win->x, win->y, win->w, WM_TITLEBAR_H, 8, COL_SURFACE0);
    /* focus accent stripe */
    if (win->focused)
        fb_fill_rect(win->x, win->y + 8, 3, WM_TITLEBAR_H - 16, COL_BLUE);
    /* traffic-light buttons */
    int bx = win->x + win->w - WM_BTN_GAP;
    int by = win->y + WM_TITLEBAR_H / 2;
    fb_fill_circle(bx,               by, WM_BTN_R, COL_RED);
    fb_fill_circle(bx - WM_BTN_GAP,  by, WM_BTN_R, COL_YELLOW);
    fb_fill_circle(bx - WM_BTN_GAP * 2, by, WM_BTN_R, COL_GREEN);
    /* title */
    int tw = font_str_width(win->title);
    int tx = win->x + (win->w - tw * 8) / 2;
    int ty = win->y + (WM_TITLEBAR_H - 16) / 2;
    font_puts(tx, ty, win->title, COL_TEXT, COL_SURFACE0);
    /* client area */
    fb_fill_rect(win->x, win->y + WM_TITLEBAR_H,
                 win->w, win->h - WM_TITLEBAR_H, COL_MANTLE);
    /* invoke paint callback */
    if (win->on_paint) win->on_paint(win);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void wm_init(void) {
    win_count = 0; next_id = 1; focused_win = NULL;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) wins[i] = NULL;
    klog(LOG_INFO, "WM: initialized");
}

window_t *wm_new(int x, int y, int w, int h, const char *title) {
    if (win_count >= WM_MAX_WINDOWS) return NULL;
    window_t *win = (window_t *)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    /* zero-init */
    for (int i = 0; i < (int)sizeof(window_t); i++) ((uint8_t *)win)[i] = 0;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->client_w = w;
    win->client_h = h - WM_TITLEBAR_H;
    strncpy_s(win->title, title, 64);
    win->state   = WIN_NORMAL;
    win->visible = 1;
    win->focused = 0;
    win->id      = next_id++;
    win->anim_frames = 8;   /* pop-in over 8 frames (~264 ms at 30 fps) */
    win->orig_x = x; win->orig_y = y;
    win->orig_w = w; win->orig_h = h;
    /* insert at front (top of z-order) */
    for (int i = win_count; i > 0; i--) wins[i] = wins[i - 1];
    wins[0] = win;
    win_count++;
    wm_focus(win);
    fb_scene_dirty = 1;   /* new window exposes desktop — need full repaint */
    return win;
}

void wm_close(window_t *win) {
    if (!win) return;
    for (int i = 0; i < win_count; i++) {
        if (wins[i] == win) {
            /* paint desktop over the area this window occupied */
            desktop_paint_rect(win->x - 2, win->y - 2,
                               win->w + WM_SHADOW_OFF + 4,
                               win->h + WM_SHADOW_OFF + 4);
            for (int j = i; j < win_count - 1; j++) wins[j] = wins[j + 1];
            wins[win_count - 1] = NULL;
            win_count--;
            if (focused_win == win) {
                focused_win = (win_count > 0) ? wins[0] : NULL;
                if (focused_win) focused_win->focused = 1;
            }
            if (win->pixels) kfree(win->pixels);
            kfree(win);
            fb_scene_dirty = 1;   /* redraw remaining windows over clean bg */
            return;
        }
    }
}

void wm_focus(window_t *win) {
    if (focused_win) focused_win->focused = 0;
    focused_win = win;
    if (win) win->focused = 1;
}

void wm_raise(window_t *win) {
    for (int i = 0; i < win_count; i++) {
        if (wins[i] == win) {
            for (int j = i; j > 0; j--) wins[j] = wins[j - 1];
            wins[0] = win;
            return;
        }
    }
}

void wm_minimize(window_t *win) {
    /* paint desktop over the area this window occupied */
    desktop_paint_rect(win->x - 2, win->y - 2,
                       win->w + WM_SHADOW_OFF + 4,
                       win->h + WM_SHADOW_OFF + 4);
    win->state = WIN_MINIMIZED;
    if (focused_win == win) {
        for (int i = 0; i < win_count; i++) {
            if (wins[i] != win && wins[i]->state != WIN_MINIMIZED) {
                wm_focus(wins[i]); break;
            }
        }
    }
    fb_scene_dirty = 1;   /* repaint remaining windows over clean bg */
}

void wm_toggle_maximize(window_t *win) {
    if (win->state == WIN_MAXIMIZED) {
        win->x = win->orig_x; win->y = win->orig_y;
        win->w = win->orig_w; win->h = win->orig_h;
        win->state = WIN_NORMAL;
    } else {
        win->orig_x = win->x; win->orig_y = win->y;
        win->orig_w = win->w; win->orig_h = win->h;
        win->x = 0; win->y = 0;
        win->w = (int)fb.width;
        win->h = (int)fb.height - 40;
        win->state = WIN_MAXIMIZED;
    }
    fb_scene_dirty = 1;   /* layout changed — full repaint */
}

void wm_move(window_t *win, int x, int y) { win->x = x; win->y = y; }
void wm_resize(window_t *win, int w, int h) {
    win->w = w; win->h = h;
    win->client_w = w;
    win->client_h = h - WM_TITLEBAR_H;
}
void wm_invalidate(window_t *win) { (void)win; /* redrawn every frame */ }

void wm_render_all(void) {
    /* draw back-to-front */
    for (int i = win_count - 1; i >= 0; i--) {
        window_t *win = wins[i];
        if (!win || !win->visible || win->state == WIN_MINIMIZED) continue;

        if (win->anim_frames > 0) {
            /* Pop-in: window scales from ~72% to 100% using ease-out-back.
             * progress = (8 - frames) * 32  →  0, 32, 64 … 256 over 8 frames */
            int progress = (8 - win->anim_frames) * 32;
            int eased    = anim_ease_out_back(progress);  /* 0-280 with overshoot */
            /* scale: map eased (0-256) onto 185-256 (72%→100%) */
            int scale = 185 + eased * 71 / 280;
            scale = anim_clamp(scale, 64, 256);

            /* Temporarily shrink window geometry for this draw call */
            int ox = win->x, oy = win->y, ow = win->w, oh = win->h;
            int dw = ow * (256 - scale) / 512;
            int dh = oh * (256 - scale) / 512;
            win->x += dw; win->y += dh;
            win->w -= dw * 2; win->h -= dh * 2;
            if (win->w < 4) win->w = 4;
            if (win->h < 4) win->h = 4;

            wm_draw_window(win);

            /* Restore real geometry */
            win->x = ox; win->y = oy; win->w = ow; win->h = oh;
            win->anim_frames--;
        } else {
            wm_draw_window(win);
        }
    }
}

void wm_handle_mouse(int x, int y, int left, int right) {
    (void)right;
    static int prev_left = 0;
    int pressed = left && !prev_left;
    prev_left = left;

    if (focused_win && focused_win->dragging && left) {
        int nx = x - focused_win->drag_ox;
        int ny = y - focused_win->drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + focused_win->w > (int)fb.width)
            nx = (int)fb.width - focused_win->w;
        if (ny + focused_win->h > (int)fb.height - 40)
            ny = (int)fb.height - 40 - focused_win->h;
        if (nx != focused_win->x || ny != focused_win->y) {
            /* surgically repaint desktop over the OLD window footprint
               (shadow + border padding) before the window moves */
            desktop_paint_rect(focused_win->x - 2, focused_win->y - 2,
                               focused_win->w + WM_SHADOW_OFF + 6,
                               focused_win->h + WM_SHADOW_OFF + 6);
            wm_move(focused_win, nx, ny);
        }
        return;
    }

    if (!pressed) return;

    for (int i = 0; i < win_count; i++) {
        window_t *win = wins[i];
        if (!win || !win->visible || win->state == WIN_MINIMIZED) continue;

        int bx = win->x + win->w - WM_BTN_GAP;
        int by = win->y + WM_TITLEBAR_H / 2;

        /* close button */
        if (point_in_circle(x, y, bx, by, WM_BTN_R)) {
            if (win->on_close) win->on_close(win);
            else wm_close(win);
            return;
        }
        /* min button */
        if (point_in_circle(x, y, bx - WM_BTN_GAP, by, WM_BTN_R)) {
            wm_minimize(win); return;
        }
        /* max button */
        if (point_in_circle(x, y, bx - WM_BTN_GAP * 2, by, WM_BTN_R)) {
            wm_toggle_maximize(win); return;
        }
        /* title bar drag */
        if (point_in_rect(x, y, win->x, win->y, win->w, WM_TITLEBAR_H)) {
            wm_raise(win); wm_focus(win);
            win->dragging = 1;
            win->drag_ox = x - win->x;
            win->drag_oy = y - win->y;
            return;
        }
        /* client area click */
        if (point_in_rect(x, y, win->x, win->y + WM_TITLEBAR_H,
                          win->w, win->h - WM_TITLEBAR_H)) {
            wm_raise(win); wm_focus(win);
            if (win->on_click)
                win->on_click(win,
                    x - win->x,
                    y - win->y - WM_TITLEBAR_H,
                    left ? 1 : 2);
            return;
        }
    }
}

void wm_handle_mouse_release(int x, int y) {
    (void)x; (void)y;
    if (focused_win && focused_win->dragging) {
        focused_win->dragging = 0;
        fb_scene_dirty = 1;  /* one final full repaint to clean up any artifacts */
    }
}

void wm_handle_key(char key) {
    if (focused_win && focused_win->on_key)
        focused_win->on_key(focused_win, key);
}

window_t *wm_focused(void) { return focused_win; }

void wm_put_pixel(window_t *w, int x, int y, uint32_t color) {
    fb_put_pixel(w->x + x, w->y + WM_TITLEBAR_H + y, color);
}

int wm_window_count(void) { return win_count; }
window_t *wm_get_window(int idx) {
    if (idx < 0 || idx >= win_count) return NULL;
    return wins[idx];
}
