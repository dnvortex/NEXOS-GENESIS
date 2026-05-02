/* NexOS — userspace/libc/stdlib.c | Standard library | MIT License */
#include "stdlib.h"
#include "string.h"

/* Syscall interface */
static long syscall(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

int atoi(const char *s) {
    int sign = 1, result = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

long atol(const char *s) {
    long sign = 1, result = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

char *itoa(int value, char *buf, int base) {
    char *p = buf;
    char tmp[32];
    int i = 0;
    const char *digits = "0123456789abcdef";
    unsigned int uval;

    if (base == 10 && value < 0) {
        *p++ = '-';
        uval = (unsigned int)(-value);
    } else {
        uval = (unsigned int)value;
    }

    do {
        tmp[i++] = digits[uval % base];
        uval /= base;
    } while (uval);

    while (i > 0) *p++ = tmp[--i];
    *p = 0;
    return buf;
}

/* Simple bump allocator backed by sbrk syscall */
static char  heap_mem[1024 * 1024];  /* 1MB embedded heap for user processes */
static size_t heap_pos = 0;

void *malloc(size_t size) {
    size = (size + 7) & ~7;  /* align to 8 */
    if (heap_pos + size > sizeof(heap_mem)) return NULL;
    void *ptr = &heap_mem[heap_pos];
    heap_pos += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
    /* Simple bump allocator doesn't free — acceptable for early userspace */
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, size);
    free(ptr);
    return new_ptr;
}

void exit(int code) {
    syscall(60, (long)code, 0, 0);   /* SYS_EXIT = 60 (Linux x86_64 ABI) */
    for (;;);
}

int abs(int x) { return x < 0 ? -x : x; }

static unsigned int rand_seed = 12345;
int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

void srand(unsigned int seed) {
    rand_seed = seed;
}
