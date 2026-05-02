/* NexOS — kernel/mm/pmm.c | Bitmap physical memory manager | MIT License */
#include "pmm.h"
#include "../kernel.h"

#define PMM_BITMAP_SIZE (1024 * 1024 / 32)  /* covers 4GB at 4K pages */
static uint32_t pmm_bitmap[PMM_BITMAP_SIZE];
static uint64_t pmm_total_pages  = 0;
static uint64_t pmm_free_pages   = 0;
static uint64_t pmm_last_alloc   = 0;

static void bitmap_set(uint64_t bit) {
    pmm_bitmap[bit / 32] |= (1u << (bit % 32));
}
static void bitmap_clear(uint64_t bit) {
    pmm_bitmap[bit / 32] &= ~(1u << (bit % 32));
}
static int bitmap_test(uint64_t bit) {
    return !!(pmm_bitmap[bit / 32] & (1u << (bit % 32)));
}

void pmm_init(uint64_t mem_lower, uint64_t mem_upper) {
    UNUSED(mem_lower);
    /* Mark everything used */
    for (int i = 0; i < PMM_BITMAP_SIZE; i++) pmm_bitmap[i] = 0xFFFFFFFF;
    pmm_total_pages = mem_upper / PAGE_SIZE;
    pmm_free_pages  = 0;
    /* Reserve first 1MB (256 pages) */
    for (uint64_t i = 0; i < 256; i++) bitmap_set(i);
    klog(LOG_INFO, "PMM: initialized, total=%llu KB", mem_upper / 1024);
}

void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = size / PAGE_SIZE;
    for (uint64_t i = start; i < start + count; i++) {
        if (i < 256) continue; /* keep first 1MB reserved */
        bitmap_clear(i);
        pmm_free_pages++;
    }
}

void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = size / PAGE_SIZE;
    for (uint64_t i = start; i < start + count; i++) {
        bitmap_set(i);
        if (pmm_free_pages > 0) pmm_free_pages--;
    }
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t i = pmm_last_alloc; i < pmm_total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_free_pages--;
            pmm_last_alloc = i;
            return i * PAGE_SIZE;
        }
    }
    /* Wrap around */
    for (uint64_t i = 256; i < pmm_last_alloc; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_free_pages--;
            pmm_last_alloc = i;
            return i * PAGE_SIZE;
        }
    }
    klog(LOG_ERROR, "PMM: out of physical memory!");
    return 0;
}

void pmm_free_page(uint64_t addr) {
    uint64_t bit = addr / PAGE_SIZE;
    if (bitmap_test(bit)) {
        bitmap_clear(bit);
        pmm_free_pages++;
    }
}

uint64_t pmm_get_free_memory(void) {
    return pmm_free_pages * PAGE_SIZE;
}

uint64_t pmm_get_total_memory(void) {
    return pmm_total_pages * PAGE_SIZE;
}
