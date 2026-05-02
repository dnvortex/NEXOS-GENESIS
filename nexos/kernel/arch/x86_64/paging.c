/* NexOS — kernel/arch/x86_64/paging.c | 4-level paging setup | MIT License */
#include "paging.h"
#include "../../kernel.h"
#include "../../mm/pmm.h"

/* Page flags */
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITE    (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_HUGE     (1ULL << 7)

static uint64_t *pml4 = NULL;

static uint64_t *get_or_create(uint64_t *table, int idx) {
    if (!(table[idx] & PAGE_PRESENT)) {
        uint64_t page = pmm_alloc_page();
        if (!page) return NULL;
        uint8_t *p = (uint8_t *)page;
        for (int i = 0; i < 4096; i++) p[i] = 0;
        table[idx] = page | PAGE_PRESENT | PAGE_WRITE;
    }
    return (uint64_t *)(table[idx] & ~0xFFFULL);
}

void vmm_map_page(uint64_t *pml4_table, uint64_t virt, uint64_t phys, uint64_t flags) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create(pml4_table, pml4_idx);
    if (!pdpt) return;
    uint64_t *pd   = get_or_create(pdpt, pdpt_idx);
    if (!pd) return;
    uint64_t *pt   = get_or_create(pd, pd_idx);
    if (!pt) return;

    pt[pt_idx] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT;
}

void vmm_unmap_page(uint64_t *pml4_table, uint64_t virt) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4_table[pml4_idx] & PAGE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4_table[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;
    __asm__ volatile ("invlpg [%0]" :: "r"(virt) : "memory");
}

uint64_t vmm_get_physical(uint64_t *pml4_table, uint64_t virt) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4_table[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4_table[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFF);
}

void paging_init(void) {
    uint64_t page = pmm_alloc_page();
    pml4 = (uint64_t *)page;
    uint8_t *p = (uint8_t *)pml4;
    for (int i = 0; i < 4096; i++) p[i] = 0;

    /* Identity map first 4MB */
    for (uint64_t addr = 0; addr < 0x400000; addr += PAGE_SIZE) {
        vmm_map_page(pml4, addr, addr, PAGE_WRITE);
    }

    /* Load new page directory */
    __asm__ volatile ("mov cr3, %0" :: "r"((uint64_t)pml4) : "memory");

    klog(LOG_INFO, "VMM: 4-level paging enabled, first 4MB identity-mapped");
}

uint64_t *paging_get_pml4(void) {
    return pml4;
}
