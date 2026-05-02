/* NexOS — kernel/kernel.c | Kernel main entry point | MIT License */
#include "kernel.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/rtc.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "fs/procfs.h"
#include "net/net.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "proc/syscall.h"
#include <stdarg.h>
#include <stdint.h>

/* Multiboot2 structures */
#define MB2_TAG_END  0
#define MB2_TAG_MMAP 6

typedef struct { uint32_t total_size; uint32_t reserved; } __attribute__((packed)) mb2_info_t;
typedef struct { uint32_t type; uint32_t size; }           __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type; uint32_t size;
    uint64_t base_addr; uint64_t length; uint32_t mtype;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
    uint32_t type; uint32_t size;
    uint32_t entry_size; uint32_t entry_version;
    mb2_mmap_entry_t entries[];
} __attribute__((packed)) mb2_tag_mmap_t;

/* ---- Kernel log ---- */
void klog(log_level_t level, const char *fmt, ...) {
    static const char *lnames[] = {"DEBUG","INFO","WARN","ERROR","PANIC"};
    static const vga_color_t lcolors[] = {
        VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY, VGA_COLOR_LIGHT_BROWN,
        VGA_COLOR_LIGHT_RED,  VGA_COLOR_LIGHT_RED
    };

    uint64_t ticks = timer_get_ticks();
    serial_printf("[%08llu][%s] ", ticks, lnames[level]);
    vga_set_color(lcolors[level], VGA_COLOR_BLACK);
    vga_printf("[%s] ", lnames[level]);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    char buf[512];
    va_list args;
    va_start(args, fmt);
    int bi = 0;
    const char *f = fmt;
    while (*f && bi < 510) {
        if (*f != '%') { buf[bi++] = *f++; continue; }
        f++;
        switch (*f) {
        case 'd': { int64_t v = va_arg(args,int64_t);
            if(v<0){buf[bi++]='-';v=-v;} char t[20];int ti=0;
            do{t[ti++]='0'+(int)(v%10);v/=10;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 'u': { uint64_t v=va_arg(args,uint64_t); char t[20];int ti=0;
            do{t[ti++]='0'+(int)(v%10);v/=10;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 'x': { uint64_t v=va_arg(args,uint64_t); const char*hx="0123456789abcdef";
            char t[16];int ti=0;
            do{t[ti++]=hx[v&0xF];v>>=4;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 's': { const char*s=va_arg(args,const char*); if(!s)s="(null)";
            while(*s&&bi<510){buf[bi++]=*s++;} break; }
        case 'p': { uint64_t v=va_arg(args,uint64_t); buf[bi++]='0';buf[bi++]='x';
            const char*hx="0123456789abcdef";char t[16];int ti=0;
            do{t[ti++]=hx[v&0xF];v>>=4;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 'l': { f++;
            if(*f=='l')f++;
            if(*f=='u'){uint64_t v=va_arg(args,uint64_t);char t[20];int ti=0;
                do{t[ti++]='0'+(int)(v%10);v/=10;}while(v);
                while(ti>0&&bi<510){buf[bi++]=t[--ti];}}
            else if(*f=='x'){uint64_t v=va_arg(args,uint64_t);const char*hx="0123456789abcdef";
                char t[16];int ti=0;
                do{t[ti++]=hx[v&0xF];v>>=4;}while(v);
                while(ti>0&&bi<510){buf[bi++]=t[--ti];}}
            else if(*f=='d'){int64_t v=va_arg(args,int64_t);
                if(v<0){buf[bi++]='-';v=-v;}char t[20];int ti=0;
                do{t[ti++]='0'+(int)(v%10);v/=10;}while(v);
                while(ti>0&&bi<510){buf[bi++]=t[--ti];}}
            break; }
        case '%': buf[bi++]='%'; break;
        default:  buf[bi++]='%'; buf[bi++]=*f; break;
        }
        f++;
    }
    va_end(args);
    buf[bi++]='\n'; buf[bi]=0;

    serial_puts(buf);
    vga_puts(buf);

    if (level == LOG_PANIC) { cli(); for(;;) hlt(); }
}

void kpanic(const char *fmt, ...) { (void)fmt; klog(LOG_PANIC, "PANIC — system halted"); }

/* ---- Static kernel heap (2 MB) ---- */
#define HEAP_SIZE (2 * 1024 * 1024)
static uint8_t kernel_heap_area[HEAP_SIZE] __attribute__((aligned(4096)));

/* ---- Init process entry (defined in userspace/init/init.c) ---- */
extern void init_main(void);

/* ---- Kernel main ---- */
void kernel_main(uint32_t mb2_magic, mb2_info_t *mb2_info) {
    /* 1. Serial */
    serial_init();
    serial_puts("\n=== NexOS Kernel Booting ===\n");

    /* 2. VGA */
    vga_init();

    /* 3. GDT (with TSS for ring 0/3 switching) */
    gdt_init();

    /* 4. IDT */
    idt_init();
    sti();

    /* 5. Parse Multiboot2 memory map */
    uint64_t mem_upper = 256 * 1024 * 1024;
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
                    if (e->mtype == 1) pmm_init_region(e->base_addr, e->length);
                    if (e->base_addr + e->length > mem_upper)
                        mem_upper = e->base_addr + e->length;
                }
            }
            uint32_t sz = tag->size;
            if (sz % 8) sz += 8 - (sz % 8);
            tag_ptr += sz;
        }
    } else {
        pmm_init_region(0x100000, 63 * 1024 * 1024);
        klog(LOG_WARN, "No valid Multiboot2 info — using fallback memory map");
    }

    /* Reserve first 4 MB (kernel image + page tables) */
    pmm_deinit_region(0, 4 * 1024 * 1024);

    /* 6. Paging / VMM */
    paging_init();
    vmm_init();

    /* 7. Heap */
    heap_init(kernel_heap_area, HEAP_SIZE);

    /* 8. Drivers */
    timer_init(1000);
    keyboard_init();
    ata_init();

    /* 9. PCI enumeration */
    pci_init();

    /* 10. RTC */
    rtc_init();

    /* 11. Network stub */
    net_init();

    /* 12. VFS + ramfs */
    vfs_init();
    vfs_node_t *ramfs_root = ramfs_create_root();
    if (ramfs_root) {
        vfs_mount("/", ramfs_root);
    } else {
        klog(LOG_ERROR, "Failed to create ramfs root");
    }

    /* 13. Process scheduler + syscall interface */
    proc_init();
    scheduler_init();
    syscall_init();

    klog(LOG_INFO, "PMM: %llu MB free of %llu MB total",
         pmm_get_free_memory() / (1024*1024),
         pmm_get_total_memory() / (1024*1024));

    /* 14. /proc filesystem (needs heap + proc table) */
    procfs_init();

    /* 15. Create init process (PID 1) and enter ring 3 */
    klog(LOG_INFO, "Launching init process (PID 1)...");
    process_t *init = proc_create("init", init_main, 9);
    if (!init) {
        klog(LOG_ERROR, "Failed to create init process");
        cli(); for(;;) hlt();
    }
    scheduler_add(init);

    /*
     * Drop to ring 3 — this is a one-way IRET.
     * init_main will call nsh_main which loops forever.
     * On return from the shell, init halts the system.
     */
    proc_enter_ring3(init);

    /* Never reached */
    klog(LOG_WARN, "Kernel idle — should never reach here");
    for (;;) __asm__ volatile ("sti; hlt");
}
