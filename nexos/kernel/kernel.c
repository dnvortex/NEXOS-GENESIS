/* NexOS — kernel/kernel.c | Kernel main entry point | MIT License */
#include "kernel.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
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

/* ── Linker symbols — used by PMM to precisely reserve the kernel image ──── */
extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

/* ── Multiboot2 structures (corrected field layout) ─────────────────────── */
#define MB2_TAG_END  0
#define MB2_TAG_MMAP 6

typedef struct { uint32_t total_size; uint32_t reserved; }
    __attribute__((packed)) mb2_info_t;

typedef struct { uint32_t type; uint32_t size; }
    __attribute__((packed)) mb2_tag_t;

/*
 * FIXED: the original struct had spurious uint32_t type + size fields at the
 * start (8 bytes that do NOT exist in an mmap entry), pushing base_addr to
 * offset 8 and making mtype read garbage from the next entry.
 *
 * Correct Multiboot2 mmap entry layout (spec §3.6.8):
 *   uint64_t base_addr
 *   uint64_t length
 *   uint32_t type        (1 = available RAM)
 *   uint32_t reserved
 */
typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t mtype;     /* 1 = usable RAM */
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* entries follow; stride = entry_size, NOT sizeof(mb2_mmap_entry_t) */
} __attribute__((packed)) mb2_tag_mmap_t;

/* ── Kernel log ─────────────────────────────────────────────────────────── */
void klog(log_level_t level, const char *fmt, ...) {
    static const char *lnames[]  = {"DEBUG","INFO","WARN","ERROR","PANIC"};
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
        case 'x': { uint64_t v=va_arg(args,uint64_t);
            const char *hx="0123456789abcdef"; char t[16];int ti=0;
            do{t[ti++]=hx[v&0xF];v>>=4;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 's': { const char *s=va_arg(args,const char*); if(!s)s="(null)";
            while(*s&&bi<510){buf[bi++]=*s++;} break; }
        case 'p': { uint64_t v=va_arg(args,uint64_t); buf[bi++]='0';buf[bi++]='x';
            const char *hx="0123456789abcdef";char t[16];int ti=0;
            do{t[ti++]=hx[v&0xF];v>>=4;}while(v);
            while(ti>0&&bi<510){buf[bi++]=t[--ti];} break; }
        case 'l': { f++;
            if(*f=='l') f++;
            if(*f=='u'){uint64_t v=va_arg(args,uint64_t);char t[20];int ti=0;
                do{t[ti++]='0'+(int)(v%10);v/=10;}while(v);
                while(ti>0&&bi<510){buf[bi++]=t[--ti];}}
            else if(*f=='x'){uint64_t v=va_arg(args,uint64_t);
                const char *hx="0123456789abcdef";char t[16];int ti=0;
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

void kpanic(const char *fmt, ...) {
    (void)fmt;
    klog(LOG_PANIC, "PANIC — system halted");
}

/* ── Heap lives at a fixed physical address, NOT in BSS ─────────────────────
 * HEAP_START / HEAP_SIZE are defined in mm/heap.h.
 * boot.asm identity-maps 32 MB (16×2 MB huge pages); the 8 MB window at
 * 18–26 MB (0x1200000–0x1A00000) is always accessible before paging_init.
 * PMM explicitly reserves that range below, so no driver can reclaim it.
 * Removing the static array keeps kernel_end ≈ 1.5 MB and avoids the risk
 * of GRUB mis-handling a 17 MB BSS zero-fill. */

/* ── Init process entry (userspace/init/init.c) ─────────────────────────── */
extern void init_main(void);

/* ── Kernel main ─────────────────────────────────────────────────────────── */
void kernel_main(uint32_t mb2_magic, mb2_info_t *mb2_info) {

    /*
     * ── Step 0: Early VGA sentinel ─────────────────────────────────────────
     * Prove we reached kernel_main in 64-bit mode before any subsystem runs.
     * boot.asm wrote "OK" at columns 0-1; we add "KMAIN" at columns 2-6.
     */
    volatile uint16_t *vga_early = (volatile uint16_t *)0xB8000;
    vga_early[2] = 0x0F4B;  /* 'K' */
    vga_early[3] = 0x0F4D;  /* 'M' */
    vga_early[4] = 0x0F41;  /* 'A' */
    vga_early[5] = 0x0F49;  /* 'I' */
    vga_early[6] = 0x0F4E;  /* 'N' */

    /* ── 1. VGA (screen output must come first) ──────────────────────────── */
    vga_init();

    /* ── 2. Serial (debug output) ────────────────────────────────────────── */
    serial_init();
    serial_puts("\n=== NexOS Kernel Booting ===\n");

    /* ── 3. GDT (with TSS for ring-0 / ring-3 switching) ────────────────── */
    gdt_init();

    /* ── 4. IDT + enable interrupts ──────────────────────────────────────── */
    idt_init();
    sti();

    /* ── 5. Physical Memory Manager ──────────────────────────────────────── */

    /*
     * Start with an upper-bound of 256 MB; the Multiboot2 memory map may
     * extend this.  pmm_init() marks every frame as reserved.
     */
    uint64_t mem_upper = 256ULL * 1024 * 1024;
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
                    /*
                     * FIXED: iterate by entry_size bytes, not by
                     * sizeof(mb2_mmap_entry_t), to handle varying
                     * entry sizes correctly.
                     */
                    mb2_mmap_entry_t *e = (mb2_mmap_entry_t *)(
                        (uint8_t *)mmap + 16 + (uint64_t)i * mmap->entry_size);

                    if (e->mtype == 1) {
                        pmm_init_region(e->base_addr, e->length);
                    }
                    if (e->base_addr + e->length > mem_upper) {
                        mem_upper = e->base_addr + e->length;
                    }
                }
            }

            /* Tags are 8-byte aligned */
            uint32_t sz = tag->size;
            if (sz % 8) sz += 8 - (sz % 8);
            tag_ptr += sz;
        }
    } else {
        /* Fallback: assume 63 MB of conventional RAM above 1 MB */
        pmm_init_region(0x100000, 63ULL * 1024 * 1024);
        klog(LOG_WARN, "No valid Multiboot2 info — using fallback memory map");
    }

    /*
     * Re-mark reserved regions as used (precise reservations).
     *
     * Reserve exactly:
     *   a) First 1 MB — BIOS/legacy I/O area
     *   b) Kernel image — from linker symbol kernel_start to kernel_end
     *      (code + rodata + data + BSS: PMM bitmap, boot tables, boot stack,
     *      nsh/init statics).  BSS is now ~1.5 MB; no heap array inside it.
     *   c) Fixed heap window at 0x1200000 (18 MB) — 8 MB for the allocator.
     *
     * pmm_deinit_region() is safe: decrements pmm_free_pages only when the
     * frame was actually free, preventing counter corruption.
     */
    pmm_deinit_region(0, 0x100000);   /* first 1 MB (BIOS/legacy) */

    uint64_t kstart = (uint64_t)(uintptr_t)kernel_start;
    uint64_t kend   = (uint64_t)(uintptr_t)kernel_end;
    pmm_deinit_region(kstart, kend - kstart);  /* kernel image + BSS */

    /* Reserve the fixed heap window so PMM never hands it to a driver */
    pmm_deinit_region(HEAP_START, HEAP_SIZE);

    klog(LOG_INFO, "PMM: kernel 0x%x–0x%x (%llu KB), heap 0x%x–0x%x (%u MB)",
         kstart, kend, (kend - kstart) / 1024,
         (uint64_t)HEAP_START, (uint64_t)(HEAP_START + HEAP_SIZE),
         (unsigned)(HEAP_SIZE >> 20));
    klog(LOG_INFO, "PMM: %llu MB free of %llu MB total (%llu frames)",
         pmm_get_free_memory()  / (1024 * 1024),
         pmm_get_total_memory() / (1024 * 1024),
         pmm_get_free_frames());

    /* ── 6. Heap — fixed 8 MB at physical 0x1200000 (18 MB mark) ──────────
     * boot.asm maps 32 MB; this address is always valid.  No BSS array.   */
    heap_init((void *)HEAP_START, HEAP_SIZE);

    /* ── 7. Paging — inherits boot.asm CR3, no page-table teardown ────────── */
    paging_init();
    vmm_init();

    /* ── 8. PIT timer at 1000 Hz ──────────────────────────────────────────── */
    timer_init(1000);

    /* ── 9. PS/2 keyboard ─────────────────────────────────────────────────── */
    keyboard_init();

    /* ── 10. ATA PIO disk driver ──────────────────────────────────────────── */
    ata_init();

    /* ── 11. PCI bus enumeration ──────────────────────────────────────────── */
    pci_init();

    /* ── 12. CMOS Real-Time Clock ─────────────────────────────────────────── */
    rtc_init();

    /* ── 13. Network stub ─────────────────────────────────────────────────── */
    net_init();

    /* ── 14. VFS + ramfs ──────────────────────────────────────────────────── */
    vfs_init();
    vfs_node_t *ramfs_root = ramfs_create_root();
    if (ramfs_root) {
        vfs_mount("/", ramfs_root);
    } else {
        klog(LOG_ERROR, "Failed to create ramfs root");
    }

    /* ── 15. Process table, scheduler, syscall gate ───────────────────────── */
    proc_init();
    scheduler_init();
    syscall_init();

    /* ── 16. /proc filesystem (needs heap + process table) ────────────────── */
    procfs_init();

    /* ── 17. Create init process (PID 1) and drop to ring 3 ──────────────── */
    klog(LOG_INFO, "Launching init process (PID 1)...");
    process_t *init = proc_create("init", init_main, 9);
    if (!init) {
        klog(LOG_ERROR, "Failed to create init process");
        cli(); for(;;) hlt();
    }
    scheduler_add(init);

    /*
     * Drop to ring 3 — one-way IRET.
     * init_main starts nsh which loops forever reading user input.
     */
    proc_enter_ring3(init);

    /* Never reached */
    klog(LOG_WARN, "Kernel idle — should never reach here");
    for (;;) __asm__ volatile ("sti; hlt");
}
