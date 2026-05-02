/* NexOS — kernel/drivers/console.c | Framebuffer text console | MIT License */
#include "console.h"
#include "fb.h"
#include "font.h"
#include <stdarg.h>
#include <stdint.h>

#define CON_COLS   128
#define CON_ROWS   48
#define CON_PAD_X   8
#define CON_PAD_Y   8
#define CON_CHAR_W  8
#define CON_CHAR_H 16

static int      con_x  = 0;
static int      con_y  = 0;
static uint32_t con_fg = 0xCDD6F4; /* COL_TEXT */
static uint32_t con_bg = 0x1E1E2E; /* COL_BASE */

void console_init(void) {
    con_x = 0; con_y = 0;
    con_fg = 0xCDD6F4;
    con_bg = 0x1E1E2E;
}

static void con_newline(void) {
    con_x = 0;
    con_y++;
    if (con_y >= CON_ROWS) {
        fb_scroll_up(CON_CHAR_H, con_bg);
        con_y = CON_ROWS - 1;
    }
}

void console_putchar(char c) {
    if (!fb.initialized) return;
    switch (c) {
    case '\n':
        con_newline();
        break;
    case '\r':
        con_x = 0;
        break;
    case '\t':
        con_x = (con_x + 8) & ~7;
        if (con_x >= CON_COLS) con_newline();
        break;
    case '\b':
        if (con_x > 0) {
            con_x--;
            int px = CON_PAD_X + con_x * CON_CHAR_W;
            int py = CON_PAD_Y + con_y * CON_CHAR_H;
            fb_fill_rect(px, py, CON_CHAR_W, CON_CHAR_H, con_bg);
        }
        break;
    default:
        if (c < 32) break;
        {
            int px = CON_PAD_X + con_x * CON_CHAR_W;
            int py = CON_PAD_Y + con_y * CON_CHAR_H;
            font_putchar(px, py, c, con_fg, con_bg);
            con_x++;
            if (con_x >= CON_COLS) con_newline();
        }
        break;
    }
}

void console_puts(const char *s) {
    while (*s) console_putchar(*s++);
}

void console_set_color(uint32_t fg, uint32_t bg) {
    con_fg = fg;
    con_bg = bg;
}

void console_clear(void) {
    if (!fb.initialized) return;
    fb_clear(con_bg);
    con_x = 0; con_y = 0;
}

void console_set_pos(int x, int y) {
    con_x = x; con_y = y;
}

void console_get_pos(int *x, int *y) {
    if (x) *x = con_x;
    if (y) *y = con_y;
}

/* minimal printf for console */
void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    int bi = 0;
    while (*fmt && bi < 510) {
        if (*fmt != '%') { buf[bi++] = *fmt++; continue; }
        fmt++;
        int kw = 0; char kp = ' ';
        if (*fmt == '0') { kp = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { kw = kw * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
        case 'd': {
            int64_t v = va_arg(ap, int64_t);
            if (v < 0) { buf[bi++] = '-'; v = -v; }
            char t[20]; int ti = 0;
            do { t[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
            while (ti < kw && bi < 510) { buf[bi++] = kp; kw--; }
            while (ti > 0 && bi < 510) buf[bi++] = t[--ti];
            break; }
        case 'u': {
            uint64_t v = va_arg(ap, uint64_t);
            char t[20]; int ti = 0;
            do { t[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
            while (ti < kw && bi < 510) { buf[bi++] = kp; kw--; }
            while (ti > 0 && bi < 510) buf[bi++] = t[--ti];
            break; }
        case 'x': {
            uint64_t v = va_arg(ap, uint64_t);
            const char *hx = "0123456789abcdef";
            char t[16]; int ti = 0;
            do { t[ti++] = hx[v & 0xF]; v >>= 4; } while (v);
            while (ti < kw && bi < 510) { buf[bi++] = kp; kw--; }
            while (ti > 0 && bi < 510) buf[bi++] = t[--ti];
            break; }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && bi < 510) buf[bi++] = *s++;
            break; }
        case '%': buf[bi++] = '%'; break;
        default:  buf[bi++] = '%'; buf[bi++] = *fmt; break;
        }
        fmt++;
    }
    va_end(ap);
    buf[bi] = 0;
    console_puts(buf);
}
