/* NexOS — kernel/gui/launcher.c | App launcher menu | MIT License */
#include "launcher.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>
#include <stddef.h>

/* Forward declarations for app launchers */
void launch_terminal(void);
void launch_filemanager(void);
void launch_sysinfo(void);
void launch_themewin(void);
static void action_restart(void);
static void action_shutdown(void);

#define LAUNCHER_W      200
#define LAUNCHER_ITEM_H  36
#define ARRAY_LEN(a)    ((int)(sizeof(a)/sizeof((a)[0])))

typedef struct {
    char       label[32];
    char       icon_char;
    uint32_t   icon_color;
    void     (*action)(void);
} launcher_item_t;

static launcher_item_t items[] = {
    {"Terminal",    'T', 0xA6E3A1, launch_terminal},
    {"Files",       'F', 0x89B4FA, launch_filemanager},
    {"System Info", 'S', 0xCBA6F7, launch_sysinfo},
    {"Theme",       'C', 0xFAB387, launch_themewin},
    {"",             0,  0,        0},            /* separator */
    {"Restart",     'R', 0xF9E2AF, action_restart},
    {"Shutdown",    'X', 0xF38BA8, action_shutdown},
};

static int launcher_visible = 0;
static int launcher_x, launcher_y;
static int launcher_hover = -1;

static void action_restart(void) {
    /* ACPI/keyboard reset */
    __asm__ volatile (
        "mov $0xFE, %%al\n"
        "out %%al, $0x64\n"
        : : : "eax"
    );
}
static void action_shutdown(void) {
    /* ACPI shutdown via port 0x604 (QEMU/Bochs) */
    __asm__ volatile (
        "mov $0x2000, %%ax\n"
        "mov $0x604,  %%dx\n"
        "outw %%ax, %%dx\n"
        : : : "eax", "edx"
    );
}

void launcher_show(int x, int y) {
    launcher_x = x;
    int h = ARRAY_LEN(items) * LAUNCHER_ITEM_H + 8;
    launcher_y = y - h;
    if (launcher_y < 0) launcher_y = 0;
    launcher_visible = 1;
}

void launcher_hide(void)    { launcher_visible = 0; }
int  launcher_is_visible(void) { return launcher_visible; }

void launcher_draw(void) {
    if (!launcher_visible) return;
    int h = ARRAY_LEN(items) * LAUNCHER_ITEM_H + 8;
    fb_fill_rounded_rect(launcher_x, launcher_y, LAUNCHER_W, h, 10, COL_SURFACE0);
    fb_draw_rect_outline(launcher_x, launcher_y, LAUNCHER_W, h, COL_SURFACE2, 1);

    for (int i = 0; i < ARRAY_LEN(items); i++) {
        if (!items[i].label[0]) {
            /* separator */
            fb_fill_rect(launcher_x + 12,
                         launcher_y + 4 + i * LAUNCHER_ITEM_H + 14,
                         LAUNCHER_W - 24, 1, COL_SURFACE1);
            continue;
        }
        int iy = launcher_y + 4 + i * LAUNCHER_ITEM_H;
        if (launcher_hover == i)
            fb_fill_rounded_rect(launcher_x + 4, iy + 2,
                                 LAUNCHER_W - 8, LAUNCHER_ITEM_H - 4, 6, COL_SURFACE1);
        fb_fill_circle(launcher_x + 22, iy + 18, 10, items[i].icon_color);
        char ic[2] = {items[i].icon_char, 0};
        font_puts(launcher_x + 18, iy + 10, ic, COL_BASE, items[i].icon_color);
        font_puts(launcher_x + 42, iy + 10, items[i].label, COL_TEXT, COL_SURFACE0);
    }
}

void launcher_handle_click(int x, int y) {
    if (!launcher_visible) return;
    /* clicked outside → hide */
    int h = ARRAY_LEN(items) * LAUNCHER_ITEM_H + 8;
    if (x < launcher_x || x >= launcher_x + LAUNCHER_W ||
        y < launcher_y || y >= launcher_y + h) {
        launcher_hide(); return;
    }
    for (int i = 0; i < ARRAY_LEN(items); i++) {
        int iy = launcher_y + 4 + i * LAUNCHER_ITEM_H;
        if (y >= iy && y < iy + LAUNCHER_ITEM_H) {
            if (items[i].action) items[i].action();
            launcher_hide();
            return;
        }
    }
    launcher_hide();
}

void launcher_handle_key(char key) {
    if (key == 27) { launcher_hide(); return; } /* Escape */
}
