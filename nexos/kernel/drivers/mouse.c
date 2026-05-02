/* NexOS — kernel/drivers/mouse.c | PS/2 mouse + software cursor | MIT License */
#include "mouse.h"
#include "fb.h"
#include "../kernel.h"
#include "../arch/x86_64/idt.h"

void irq_install_handler(int irq, void (*handler)(registers_t *));

/* ── PS/2 state ────────────────────────────────────────────────────────── */
static int     mouse_x;
static int     mouse_y;
static uint8_t mouse_btns;
static uint8_t mouse_cycle;
static uint8_t mouse_bytes[3];
static int     mouse_needs_redraw = 0;

/* ── Cursor ─────────────────────────────────────────────────────────────── */
#define CUR_W 12
#define CUR_H 20
static uint32_t cursor_save[CUR_W * CUR_H];
static int      cursor_saved_x = -1;
static int      cursor_saved_y = -1;

static const uint8_t cursor_bmp[CUR_H][CUR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,1,1,1,0,0},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

/* ── I/O helpers ─────────────────────────────────────────────────────────── */
static void mouse_wait_write(void) {
    int t = 100000;
    while (t-- && (io_inb(0x64) & 2));
}
static void mouse_wait_read(void) {
    int t = 100000;
    while (t-- && !(io_inb(0x64) & 1));
}
static void mouse_write(uint8_t val) {
    mouse_wait_write(); io_outb(0x64, 0xD4);
    mouse_wait_write(); io_outb(0x60, val);
}
static uint8_t mouse_read(void) {
    mouse_wait_read();
    return io_inb(0x60);
}

/* ── IRQ12 handler ───────────────────────────────────────────────────────── */
static void mouse_irq_handler(registers_t *r) {
    (void)r;
    uint8_t b = io_inb(0x60);
    /* byte 0 must have bit3 set */
    if (mouse_cycle == 0 && !(b & 0x08)) return;
    mouse_bytes[mouse_cycle++] = b;
    if (mouse_cycle == 3) {
        mouse_cycle = 0;
        mouse_btns = mouse_bytes[0] & 0x07;
        int dx = (int)(int8_t)mouse_bytes[1];
        int dy = -(int)(int8_t)mouse_bytes[2];
        if (mouse_bytes[0] & 0x40) dx = (dx > 0) ? -127 : 127;
        if (mouse_bytes[0] & 0x80) dy = (dy > 0) ? -127 : 127;
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (fb.initialized) {
            if (mouse_x >= (int)fb.width)  mouse_x = (int)fb.width  - 1;
            if (mouse_y >= (int)fb.height) mouse_y = (int)fb.height - 1;
        }
        mouse_needs_redraw = 1;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void mouse_init(void) {
    mouse_x = (int)(fb.initialized ? fb.width  / 2 : 512);
    mouse_y = (int)(fb.initialized ? fb.height / 2 : 384);
    mouse_cycle = 0; mouse_btns = 0;

    mouse_wait_write(); io_outb(0x64, 0xA8);
    mouse_wait_write(); io_outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t status = io_inb(0x60) | 2;
    mouse_wait_write(); io_outb(0x64, 0x60);
    mouse_wait_write(); io_outb(0x60, status);
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();

    irq_install_handler(12, mouse_irq_handler);
    klog(LOG_INFO, "Mouse: PS/2 initialized");
}

int     mouse_get_x(void)    { return mouse_x; }
int     mouse_get_y(void)    { return mouse_y; }
uint8_t mouse_get_btns(void) { return mouse_btns; }
int     mouse_left(void)     { return mouse_btns & 1; }
int     mouse_right(void)    { return mouse_btns & 2; }
int     mouse_needs_update(void) {
    if (mouse_needs_redraw) { mouse_needs_redraw = 0; return 1; }
    return 0;
}

void cursor_restore(void) {
    if (cursor_saved_x < 0) return;
    for (int cy = 0; cy < CUR_H; cy++)
        for (int cx = 0; cx < CUR_W; cx++)
            fb_put_pixel(cursor_saved_x + cx, cursor_saved_y + cy,
                         cursor_save[cy * CUR_W + cx]);
    cursor_saved_x = -1; cursor_saved_y = -1;
}

void cursor_draw(int x, int y) {
    cursor_restore();
    cursor_saved_x = x; cursor_saved_y = y;
    for (int cy = 0; cy < CUR_H; cy++)
        for (int cx = 0; cx < CUR_W; cx++)
            cursor_save[cy * CUR_W + cx] = fb_get_pixel(x + cx, y + cy);
    for (int cy = 0; cy < CUR_H; cy++) {
        for (int cx = 0; cx < CUR_W; cx++) {
            if      (cursor_bmp[cy][cx] == 1) fb_put_pixel(x + cx, y + cy, 0x000000);
            else if (cursor_bmp[cy][cx] == 2) fb_put_pixel(x + cx, y + cy, 0xFFFFFF);
        }
    }
}
