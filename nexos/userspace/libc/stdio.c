/* NexOS — userspace/libc/stdio.c | Standard I/O via syscalls | MIT License */
#include "stdio.h"
#include "string.h"

/* Syscall wrappers */
static long sys_write(int fd, const void *buf, size_t len) {
    long ret;
    __asm__ volatile (
        "mov $1, %%rax\n"   /* SYS_WRITE = 1 (Linux x86_64 ABI) */
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((long)fd), "r"((long)buf), "r"((long)len)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

static long sys_read(int fd, void *buf, size_t len) {
    long ret;
    __asm__ volatile (
        "mov $0, %%rax\n"   /* SYS_READ = 0 (Linux x86_64 ABI) */
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((long)fd), "r"((long)buf), "r"((long)len)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

int putchar(int c) {
    char ch = (char)c;
    sys_write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    size_t len = strlen(s);
    sys_write(1, s, len);
    sys_write(1, "\n", 1);
    return 0;
}

int getchar(void) {
    char ch;
    if (sys_read(0, &ch, 1) <= 0) return EOF;
    return (int)(unsigned char)ch;
}

char *gets(char *buf) {
    int i = 0;
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        buf[i++] = (char)ch;
    }
    buf[i] = 0;
    return buf;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    int written = 0;
    if (size == 0) return 0;
    size_t max = size - 1;

#define EMIT(c) do { if ((size_t)written < max) buf[written] = (c); written++; } while(0)

    while (*fmt) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;

        int width = 0, left = 0;
        char pad = ' ';
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            long long v = is_long ? va_arg(args, long) : va_arg(args, int);
            char tmp[24]; int ti = 0;
            if (v < 0) { EMIT('-'); v = -v; }
            if (v == 0) tmp[ti++] = '0';
            while (v > 0) { tmp[ti++] = '0' + (int)(v % 10); v /= 10; }
            if (!left) { int pad_n = width - ti; while (pad_n-- > 0) EMIT(pad); }
            while (ti > 0) EMIT(tmp[--ti]);
            break;
        }
        case 'u': {
            unsigned long long v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned);
            char tmp[24]; int ti = 0;
            if (v == 0) tmp[ti++] = '0';
            while (v > 0) { tmp[ti++] = '0' + (int)(v % 10); v /= 10; }
            if (!left) { int pad_n = width - ti; while (pad_n-- > 0) EMIT(pad); }
            while (ti > 0) EMIT(tmp[--ti]);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned);
            const char *hx = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            char tmp[18]; int ti = 0;
            if (v == 0) tmp[ti++] = '0';
            while (v > 0) { tmp[ti++] = hx[v & 0xF]; v >>= 4; }
            if (!left) { int pad_n = width - ti; while (pad_n-- > 0) EMIT(pad); }
            while (ti > 0) EMIT(tmp[--ti]);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(args, void *);
            EMIT('0'); EMIT('x');
            const char *hx = "0123456789abcdef";
            char tmp[18]; int ti = 0;
            if (v == 0) tmp[ti++] = '0';
            while (v > 0) { tmp[ti++] = hx[v & 0xF]; v >>= 4; }
            while (ti > 0) EMIT(tmp[--ti]);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            if (!left) { int pad_n = width - slen; while (pad_n-- > 0) EMIT(' '); }
            while (*s) EMIT(*s++);
            break;
        }
        case 'c': EMIT((char)va_arg(args, int)); break;
        case '%': EMIT('%'); break;
        default: EMIT('%'); EMIT(*fmt); break;
        }
        fmt++;
    }
#undef EMIT

    if ((size_t)written < size) buf[written] = 0;
    else buf[size - 1] = 0;
    return written;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, 65536, fmt, args);
    va_end(args);
    return n;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sys_write(1, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf)));
    return n;
}
