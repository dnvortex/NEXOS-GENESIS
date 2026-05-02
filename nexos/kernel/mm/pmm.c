/* NexOS — kernel/mm/pmm.c | Bitmap physical memory manager | MIT License */
#include "pmm.h"
#include "../kernel.h"

/* Linker-provided symbols — mark the exact extent of the kernel binary
   (code + rodata + data + BSS, including boot page tables, PMM bitmap,
   kernel heap arena, and boot stack).  Defined in boot/linker.ld. */
extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

/* One bit per 4 KB frame; covers up to 4 GB physical address space. */
#define PMM_BITMAP_SIZE (1024 * 1024 / 32)   /* 32 768 uint32_t = 1 M bits */
static uint32_t pmm_bitmap[PMM_BITMAP_SIZE];

static uint64_t pmm_total_pages = 0;
static uint64_t pmm_free_pages  = 0;
static uint64_t pmm_last_alloc  = 0;

/* ── Bitmap helpers ─────────────────────────────────────────────────────── */

static void bitmap_set(uint64_t bit) {
    pmm_bitmap[bit / 32] |= (1u << (bit % 32));
}
static void bitmap_clear(uint64_t bit) {
    pmm_bitmap[bit / 32] &= ~(1u << (bit % 32));
}
static int bitmap_test(uint64_t bit) {
    return !!(pmm_bitmap[bit / 32] & (1u << (bit % 32)));
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * pmm_init — called once with the total physical memory extent.
 *
 * Marks every frame as reserved (bitmap = 0xFF…).  Callers must then call
 * pmm_init_region() for each available RAM region and pmm_deinit_region()
 * for every reserved region (kernel image, BIOS area, etc.).
 */
void pmm_init(uint64_t mem_lower, uint64_t mem_upper) {
    UNUSED(mem_lower);

    /* Step a: mark ALL frames as used. */
    for (int i = 0; i < PMM_BITMAP_SIZE; i++) pmm_bitmap[i] = 0xFFFFFFFF;

    pmm_total_pages = mem_upper / PAGE_SIZE;
    pmm_free_pages  = 0;
    pmm_last_alloc  = 0;

    klog(LOG_INFO, "PMM: initialized - %llu MB physical memory ceiling",
         mem_upper / (1024 * 1024));
}

/*
 * pmm_init_region — mark a physical region as available (free).
 * Called for every Multiboot2 memory-map entry with type = 1.
 * Skips frames below 1 MB (BIOS/legacy area — never handed out).
 */
void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = size / PAGE_SIZE;
    for (uint64_t i = start; i < start + count; i++) {
        if (i < 256) continue;      /* always keep first 1 MB reserved */
        if (i >= pmm_total_pages) break;
        bitmap_clear(i);
        pmm_free_pages++;
    }
}

/*
 * pmm_deinit_region — mark a physical region as reserved (used).
 *
 * BUG FIX: the old version decremented pmm_free_pages unconditionally,
 * even for frames that were never freed (e.g., BIOS pages 0-255).
 * Now we only decrement if the frame was actually free before marking.
 */
void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = size / PAGE_SIZE;
    for (uint64_t i = start; i < start + count; i++) {
        if (!bitmap_test(i)) {      /* only count it if it was free */
            pmm_free_pages--;
        }
        bitmap_set(i);
    }
}

uint64_t pmm_alloc_page(void) {
    /* Search from last allocation upwards */
    for (uint64_t i = pmm_last_alloc; i < pmm_total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (pmm_free_pages > 0) pmm_free_pages--;
            pmm_last_alloc = i + 1;
            return i * PAGE_SIZE;
        }
    }
    /* Wrap around and search from just above the first 1 MB */
    for (uint64_t i = 256; i < pmm_last_alloc; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (pmm_free_pages > 0) pmm_free_pages--;
            pmm_last_alloc = i + 1;
            return i * PAGE_SIZE;
        }
    }
    klog(LOG_ERROR, "PMM: out of physical memory!");
    return 0;
}

void pmm_free_page(uint64_t addr) {
    uint64_t bit = addr / PAGE_SIZE;
    if (bit < pmm_total_pages && bitmap_test(bit)) {
        bitmap_clear(bit);
        pmm_free_pages++;
    }
}

uint64_t pmm_get_free_memory(void)  { return pmm_free_pages  * PAGE_SIZE; }
uint64_t pmm_get_total_memory(void) { return pmm_total_pages * PAGE_SIZE; }
uint64_t pmm_get_free_frames(void)  { return pmm_free_pages; }

void pmm_print_map(void) {
    uint64_t total = pmm_total_pages * PAGE_SIZE;
    uint64_t free_ram = total > 0x01A00000 ? (total - 0x01A00000) >> 20 : 0;
    klog(LOG_INFO, "PMM memory map:");
    klog(LOG_INFO, "  0x00000000 - 0x000FFFFF  [BIOS/reserved]   1 MB");
    klog(LOG_INFO, "  0x00100000 - 0x011FFFFF  [kernel+static]  17 MB");
    klog(LOG_INFO, "  0x01200000 - 0x019FFFFF  [heap]            8 MB");
    klog(LOG_INFO, "  0x01A00000 - 0x%08x  [free RAM]     %llu MB",
         (uint32_t)(total - 1), free_ram);
    klog(LOG_INFO, "  Free: %llu frames (%llu KB)",
         pmm_free_pages, pmm_free_pages * (PAGE_SIZE / 1024));
}
