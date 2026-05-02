/* NexOS — kernel/drivers/serial.c | COM1 serial port 115200 baud | MIT License */
#include "serial.h"
#include "../kernel.h"
#include <stdarg.h>

#define COM1 0x3F8

void serial_init(void) {
    io_outb(COM1 + 1, 0x00); /* Disable interrupts */
    io_outb(COM1 + 3, 0x80); /* Enable DLAB (divisor latch) */
    io_outb(COM1 + 0, 0x01); /* Divisor low: 115200 baud (divisor=1) */
    io_outb(COM1 + 1, 0x00); /* Divisor high */
    io_outb(COM1 + 3, 0x03); /* 8 bits, no parity, 1 stop bit */
    io_outb(COM1 + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
    io_outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_transmit_empty(void) {
    return io_inb(COM1 + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_transmit_empty());
    io_outb(COM1, (uint8_t)c);
}

void serial_puts(const char *str) {
    while (*str) serial_putchar(*str++);
}

static void serial_print_num(uint64_t n, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    const char *digits = "0123456789abcdef";
    if (n == 0) { buf[i++] = '0'; }
    while (n) { buf[i++] = digits[n % base]; n /= base; }
    while (i < width) buf[i++] = pad;
    while (i > 0) serial_putchar(buf[--i]);
}

void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') { serial_putchar(*fmt++); continue; }
        fmt++;
        int width = 0; char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
            case 'd': { int64_t v = va_arg(args, int64_t); if (v < 0) { serial_putchar('-'); v = -v; } serial_print_num((uint64_t)v, 10, width, pad); break; }
            case 'u': serial_print_num(va_arg(args, uint64_t), 10, width, pad); break;
            case 'x': serial_print_num(va_arg(args, uint64_t), 16, width, pad); break;
            case 'l': {
                fmt++;
                if (*fmt == 'l') { fmt++; }
                if (*fmt == 'u') serial_print_num(va_arg(args, uint64_t), 10, width, pad);
                else if (*fmt == 'x') serial_print_num(va_arg(args, uint64_t), 16, width, pad);
                else if (*fmt == 'd') { int64_t v = va_arg(args, int64_t); if(v<0){serial_putchar('-');v=-v;} serial_print_num((uint64_t)v,10,width,pad); }
                break;
            }
            case 'p': serial_puts("0x"); serial_print_num(va_arg(args, uint64_t), 16, 16, '0'); break;
            case 's': { const char *s = va_arg(args, const char *); serial_puts(s ? s : "(null)"); break; }
            case 'c': serial_putchar((char)va_arg(args, int)); break;
            case '%': serial_putchar('%'); break;
            default:  serial_putchar('%'); serial_putchar(*fmt); break;
        }
        fmt++;
    }
    va_end(args);
}
