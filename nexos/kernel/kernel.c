/* NexOS — kernel/kernel.c | Kernel main entry point | MIT License */
#include "kernel.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "proc/syscall.h"
#include <stdarg.h>
#include <stdint.h>

/* Multiboot2 structures */
#define MB2_TAG_END       0
#define MB2_TAG_MMAP      6
#define MB2_TAG_BOOTNAME  1
#define MB2_TAG_FRAMEBUF  8

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) mb2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t mtype;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    mb2_mmap_entry_t entries[];
} __attribute__((packed)) mb2_tag_mmap_t;

/* Kernel log */
void klog(log_level_t level, const char *fmt, ...) {
    static const char *level_names[] = {"DEBUG","INFO","WARN","ERROR","PANIC"};
    static const vga_color_t level_colors[] = {
        VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY, VGA_COLOR_LIGHT_BROWN,
        VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_RED
    };

    uint64_t ticks = timer_get_ticks();

    /* Serial output */
    serial_printf("[%08llu][%s] ", ticks, level_names[level]);

    /* VGA: color-coded prefix */
    vga_set_color(level_colors[level], VGA_COLOR_BLACK);
    vga_printf("[%s] ", level_names[level]);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Format the message */
    char buf[512];
    va_list args;
    va_start(args, fmt);

    /* Simple vsnprintf inline */
    int bi = 0;
    const char *f = fmt;
    while (*f && bi < 510) {
        if (*f != '%') { buf[bi++] = *f++; continue; }
        f++;
        switch (*f) {
            case 'd': { int64_t v = va_arg(args, int64_t);
                if (v < 0) { buf[bi++] = '-'; v = -v; }
                char tmp[20]; int ti = 0;
                do { tmp[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
                while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                break; }
            case 'u': { uint64_t v = va_arg(args, uint64_t);
                char tmp[20]; int ti = 0;
                do { tmp[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
                while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                break; }
            case 'x': { uint64_t v = va_arg(args, uint64_t);
                const char *hx = "0123456789abcdef";
                char tmp[16]; int ti = 0;
                do { tmp[ti++] = hx[v & 0xF]; v >>= 4; } while (v);
                while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                break; }
            case 's': { const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && bi < 510) buf[bi++] = *s++;
                break; }
            case 'p': {
                uint64_t v = va_arg(args, uint64_t);
                buf[bi++] = '0'; buf[bi++] = 'x';
                const char *hx = "0123456789abcdef";
                char tmp[16]; int ti = 0;
                do { tmp[ti++] = hx[v & 0xF]; v >>= 4; } while (v);
                while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                break; }
            case 'l': {
                f++;
                if (*f == 'l') f++;
                if (*f == 'u') {
                    uint64_t v = va_arg(args, uint64_t);
                    char tmp[20]; int ti = 0;
                    do { tmp[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
                    while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                } else if (*f == 'x') {
                    uint64_t v = va_arg(args, uint64_t);
                    const char *hx = "0123456789abcdef";
                    char tmp[16]; int ti = 0;
                    do { tmp[ti++] = hx[v & 0xF]; v >>= 4; } while (v);
                    while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                } else if (*f == 'd') {
                    int64_t v = va_arg(args, int64_t);
                    if (v < 0) { buf[bi++] = '-'; v = -v; }
                    char tmp[20]; int ti = 0;
                    do { tmp[ti++] = '0' + (int)(v % 10); v /= 10; } while (v);
                    while (ti > 0 && bi < 510) buf[bi++] = tmp[--ti];
                }
                break; }
            case '%': buf[bi++] = '%'; break;
            default: buf[bi++] = '%'; buf[bi++] = *f; break;
        }
        f++;
    }
    va_end(args);
    buf[bi++] = '\n';
    buf[bi] = 0;

    serial_puts(buf);
    vga_puts(buf);

    if (level == LOG_PANIC) {
        cli();
        for (;;) hlt();
    }
}

void kpanic(const char *fmt, ...) {
    (void)fmt;
    klog(LOG_PANIC, "PANIC — system halted");
}

/* ---- Static kernel heap ---- */
#define HEAP_SIZE (2 * 1024 * 1024)  /* 2MB static heap */
static uint8_t kernel_heap_area[HEAP_SIZE] __attribute__((aligned(4096)));

/* ---- Init process entry ---- */
extern void init_main(void);

/* ---- Kernel main ---- */
void kernel_main(uint32_t mb2_magic, mb2_info_t *mb2_info) {
    /* 1. Serial debug */
    serial_init();
    serial_puts("\n=== NexOS Kernel Booting ===\n");

    /* 2. VGA */
    vga_init();

    /* 3. GDT */
    gdt_init();

    /* 4. IDT + ISRs */
    idt_init();
    sti();

    /* 5. Parse Multiboot2 memory map */
    uint64_t mem_upper = 256 * 1024 * 1024; /* default 256MB */
    pmm_init(0, mem_upper);

    if (mb2_magic == MULTIBOOT2_BOOTLOADER_MAGIC && mb2_info) {
        uint8_t *tag_ptr = (uint8_t *)mb2_info + 8;
        while (1) {
            mb2_tag_t *tag = (mb2_tag_t *)tag_ptr;
            if (tag->type == MB2_TAG_END) break;
            if (tag->type == MB2_TAG_MMAP) {
                mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
                uint32_t num = (mmap->size - 16) / mmap->entry_size;
                for (uint32_t i = 0; i < num; i++) {
                    mb2_mmap_entry_t *e = &mmap->entries[i];
                    if (e->mtype == 1) { /* Available */
                        pmm_init_region(e->base_addr, e->length);
                    }
                    if (e->base_addr + e->length > mem_upper)
                        mem_upper = e->base_addr + e->length;
                }
            }
            /* Align to 8 bytes */
            uint32_t sz = tag->size;
            if (sz % 8) sz += 8 - (sz % 8);
            tag_ptr += sz;
        }
    } else {
        /* No multiboot info — mark 1MB-64MB as free */
        pmm_init_region(0x100000, 63 * 1024 * 1024);
        klog(LOG_WARN, "No valid Multiboot2 info — using fallback memory map");
    }

    /* Reserve the kernel itself (first 4MB) */
    pmm_deinit_region(0, 4 * 1024 * 1024);

    /* 6. VMM + paging */
    paging_init();

    /* 7. Kernel heap */
    heap_init(kernel_heap_area, HEAP_SIZE);

    /* 8. PIT timer 1000Hz */
    timer_init(1000);

    /* 9. Keyboard */
    keyboard_init();

    /* 10. ATA disk driver */
    ata_init();

    /* 11. PCI enumeration */
    pci_init();

    /* 12. VFS + ramfs */
    vfs_init();
    vfs_node_t *ramfs_root = ramfs_create_root();
    if (ramfs_root) {
        vfs_mount("/", ramfs_root);
    } else {
        klog(LOG_ERROR, "Failed to create ramfs root");
    }

    /* 13. Process scheduler */
    proc_init();
    scheduler_init();
    syscall_init();

    klog(LOG_INFO, "PMM: %llu MB free of %llu MB total",
         pmm_get_free_memory() / (1024*1024),
         pmm_get_total_memory() / (1024*1024));

    /* 14. Launch init (PID 1) */
    klog(LOG_INFO, "Launching init process...");
    process_t *init = proc_create("init", init_main, 9);
    if (init) {
        scheduler_add(init);
        current_process = init;
        init->state = PROC_RUNNING;
    } else {
        klog(LOG_ERROR, "Failed to create init process");
    }

    /* Drop into idle loop — init_main runs "inline" for now */
    init_main();

    /* Should never reach here */
    klog(LOG_WARN, "Kernel idle loop");
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}
