/* NexOS — kernel/kernel.h | Shared kernel definitions | MIT License */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Kernel version */
#define NEXOS_VERSION_MAJOR 0
#define NEXOS_VERSION_MINOR 1
#define NEXOS_VERSION_PATCH 0
#define NEXOS_VERSION_STR   "0.1.0"
#define NEXOS_NAME          "NexOS"

/* Multiboot2 magic */
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289

/* Log levels */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_PANIC
} log_level_t;

/* Kernel log function */
void klog(log_level_t level, const char *fmt, ...);
void kpanic(const char *fmt, ...);

/* Architecture-specific */
static inline void io_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t io_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t io_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t io_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) {
    io_outb(0x80, 0);
}
static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }

/* Utility */
#define UNUSED(x) ((void)(x))
#define ALIGN_UP(val, align)   (((val) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096UL
#endif

#endif
