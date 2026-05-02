/* NexOS — kernel/drivers/vga.c | VGA text mode 80x25 driver | MIT License */
#include "vga.h"
#include "../kernel.h"
#include <stdarg.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM    ((volatile uint16_t *)0xB8000)

#define VGA_CTRL 0x3D4
#define VGA_DATA 0x3D5

static uint8_t vga_fg = VGA_COLOR_WHITE;
static uint8_t vga_bg = VGA_COLOR_BLACK;
static int     vga_row = 0;
static int     vga_col = 0;

static uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | (fg & 0x0F));
}
static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)(((uint16_t)color << 8) | (uint8_t)c);
}

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    io_outb(VGA_CTRL, 0x0F);
    io_outb(VGA_DATA, (uint8_t)(pos & 0xFF));
    io_outb(VGA_CTRL, 0x0E);
    io_outb(VGA_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    uint8_t color = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (int r = 0; r < VGA_HEIGHT - 1; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            VGA_MEM[r * VGA_WIDTH + c] = VGA_MEM[(r + 1) * VGA_WIDTH + c];
        }
    }
    for (int c = 0; c < VGA_WIDTH; c++) {
        VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = make_entry(' ', color);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    vga_fg = (uint8_t)fg;
    vga_bg = (uint8_t)bg;
}

void vga_set_cursor(int row, int col) {
    vga_row = row;
    vga_col = col;
    update_cursor();
}

void vga_clear(void) {
    uint8_t color = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEM[i] = make_entry(' ', color);
    }
    vga_row = 0;
    vga_col = 0;
    update_cursor();
}

void vga_putchar(char c) {
    uint8_t color = make_color((vga_color_t)vga_fg, (vga_color_t)vga_bg);

    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_MEM[vga_row * VGA_WIDTH + vga_col] = make_entry(' ', color);
        }
    } else {
        VGA_MEM[vga_row * VGA_WIDTH + vga_col] = make_entry(c, color);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }

    if (vga_row >= VGA_HEIGHT) {
        scroll();
    }

    update_cursor();
}

void vga_puts(const char *str) {
    while (*str) vga_putchar(*str++);
}

static void vga_print_num(uint64_t n, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    const char *digits = "0123456789abcdef";
    if (n == 0) { buf[i++] = '0'; }
    while (n) { buf[i++] = digits[n % base]; n /= base; }
    while (i < width) buf[i++] = pad;
    while (i > 0) vga_putchar(buf[--i]);
}

void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') { vga_putchar(*fmt++); continue; }
        fmt++;
        int width = 0; char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
            case 'd': { int64_t v = va_arg(args, int64_t); if (v < 0) { vga_putchar('-'); v = -v; } vga_print_num((uint64_t)v, 10, width, pad); break; }
            case 'u': vga_print_num(va_arg(args, uint64_t), 10, width, pad); break;
            case 'x': vga_print_num(va_arg(args, uint64_t), 16, width, pad); break;
            case 'p': vga_puts("0x"); vga_print_num(va_arg(args, uint64_t), 16, 16, '0'); break;
            case 's': { const char *s = va_arg(args, const char *); vga_puts(s ? s : "(null)"); break; }
            case 'c': vga_putchar((char)va_arg(args, int)); break;
            case '%': vga_putchar('%'); break;
            default:  vga_putchar('%'); vga_putchar(*fmt); break;
        }
        fmt++;
    }
    va_end(args);
}

void vga_init(void) {
    vga_clear();
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("  _   _           ___  ____  \n");
    vga_puts(" | \\ | | _____  _/ _ \\/ ___| \n");
    vga_puts(" |  \\| |/ _ \\ \\/ / | | \\___ \\ \n");
    vga_puts(" | |\\  |  __/>  <| |_| |___) |\n");
    vga_puts(" |_| \\_|\\___/_/\\_\\\\___/|____/ \n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("\n  NexOS v0.1.0  |  MIT License\n");
    vga_puts("  Booting...\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
