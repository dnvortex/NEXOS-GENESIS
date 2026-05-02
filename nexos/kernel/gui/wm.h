/* NexOS — kernel/gui/wm.h | Window Manager | MIT License */
#pragma once
#include <stdint.h>

#define WM_MAX_WINDOWS  16
#define WM_TITLEBAR_H   32
#define WM_BORDER       1
#define WM_SHADOW_OFF   4
#define WM_SHADOW_COL   0x0A0A14
#define WM_BTN_R        7
#define WM_BTN_GAP      22

typedef enum { WIN_NORMAL, WIN_MINIMIZED, WIN_MAXIMIZED } win_state_t;

typedef struct window {
    int x, y, w, h;
    char title[64];
    uint32_t *pixels;
    int client_w, client_h;
    win_state_t state;
    uint8_t focused;
    uint8_t visible;
    uint8_t dragging;
    int drag_ox, drag_oy;
    uint8_t resizing;
    void (*on_paint)(struct window *win);
    void (*on_click)(struct window *win, int x, int y, int btn);
    void (*on_key)(struct window *win, char key);
    void (*on_close)(struct window *win);
    void *userdata;
    int id;
    struct window *next;
    /* animation */
    int anim_frames;
    int orig_x, orig_y, orig_w, orig_h;
    uint8_t closing;
} window_t;

void      wm_init(void);
window_t *wm_new(int x, int y, int w, int h, const char *title);
void      wm_close(window_t *win);
void      wm_focus(window_t *win);
void      wm_raise(window_t *win);
void      wm_minimize(window_t *win);
void      wm_move(window_t *win, int x, int y);
void      wm_resize(window_t *win, int w, int h);
void      wm_invalidate(window_t *win);
void      wm_render_all(void);
void      wm_handle_mouse(int x, int y, int left, int right);
void      wm_handle_mouse_release(int x, int y);
void      wm_handle_key(char key);
window_t *wm_focused(void);
void      wm_put_pixel(window_t *w, int x, int y, uint32_t color);
void      wm_toggle_maximize(window_t *win);
int       wm_window_count(void);
window_t *wm_get_window(int idx);
