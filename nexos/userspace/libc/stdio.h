/* NexOS — userspace/libc/stdio.h | Standard I/O header | MIT License */
#ifndef STDIO_H
#define STDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

int     printf(const char *fmt, ...);
int     putchar(int c);
int     puts(const char *s);
int     getchar(void);
char   *gets(char *buf);
int     sprintf(char *buf, const char *fmt, ...);
int     snprintf(char *buf, size_t size, const char *fmt, ...);
int     vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

#define EOF (-1)

#endif
