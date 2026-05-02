/* NexOS — userspace/libc/stdlib.h | Standard library header | MIT License */
#ifndef STDLIB_H
#define STDLIB_H

#include <stdint.h>
#include <stddef.h>

int    atoi(const char *s);
long   atol(const char *s);
char  *itoa(int value, char *buf, int base);
void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);
void   exit(int code);
int    abs(int x);
int    rand(void);
void   srand(unsigned int seed);

#endif
