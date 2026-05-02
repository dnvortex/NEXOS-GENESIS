/* NexOS — kernel/gui/taskbar.c | Bottom taskbar | MIT License */
#include "taskbar.h"
#include "wm.h"
#include "launcher.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/rtc.h"
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

void taskbar_draw(void) {
    if (!fb.initialized) return;
    tb_y = (int)fb.height - TB_H;

    /* background */
    fb_fill_rect(0, tb_y, (int)fb.width, TB_H, COL_SURFACE0);
    /* top border */
    fb_fill_rect(0, tb_y, (int)fb.width, 1, COL_SURFACE1);

    /* Apps button */
    fb_fill_rounded_rect(8, tb_y + 6, 80, 28, 6, COL_SURFACE1);
    font_puts(16, tb_y + 12, "  Apps", COL_BLUE, COL_SURFACE1);

    /* Window list */
    int bx = 100;
    int wc = wm_window_count();
    for (int i = 0; i < wc; i++) {
        window_t *win = wm_get_window(i);
        if (!win || win->state == WIN_MINIMIZED) continue;
        uint32_t btn_col = win->focused ? COL_SURFACE2 : COL_SURFACE0;
        fb_fill_rounded_rect(bx, tb_y + 6, 120, 28, 6, btn_col);
        char short_title[15];
        int k = 0;
        while (k < 14 && win->title[k]) { short_title[k] = win->title[k]; k++; }
        short_title[k] = 0;
        font_puts(bx + 8, tb_y + 12, short_title, COL_TEXT, btn_col);
        bx += 128;
        if (bx > (int)fb.width - 200) break;
    }

    /* Clock */
    rtc_time_t t; rtc_get_time(&t);
    char clock_str[6];
    clock_str[0] = '0' + t.hour / 10;
    clock_str[1] = '0' + t.hour % 10;
    clock_str[2] = ':';
    clock_str[3] = '0' + t.minute / 10;
    clock_str[4] = '0' + t.minute % 10;
    clock_str[5] = 0;
    font_puts((int)fb.width - 56, tb_y + 12, clock_str, COL_TEXT, COL_SURFACE0);

    /* Memory */
    uint32_t free_mb = (uint32_t)(pmm_get_free_frames() * 4 / 1024);
    char mem_str[8];
    itoa_u(free_mb, mem_str, 1);
    int mi = 0; while (mem_str[mi]) mi++;
    mem_str[mi++] = 'M'; mem_str[mi] = 0;
    font_puts((int)fb.width - 110, tb_y + 12, mem_str, COL_SUBTEXT, COL_SURFACE0);
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
        if (bx > (int)fb.width - 200) break;
    }
}

void taskbar_update(void) {
    /* called every second — taskbar_draw() handles the update */
}
