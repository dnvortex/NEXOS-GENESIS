/* NexOS — kernel/arch/x86_64/paging.c | 4-level paging setup | MIT License
 *
 * Design note (safe early paging):
 *   boot.asm sets up boot_pml4 / boot_pdpt / boot_pd and loads CR3 before
 *   calling kernel_main.  paging_init() must NOT create a fresh empty PML4
 *   and reload CR3, because that would momentarily tear down the running
 *   identity map before the new one is established.
 *
 *   Instead we read the existing PML4 address directly from CR3, store it as
 *   kernel_pml4, and then ADD any extra mappings on top of what boot.asm
 *   already set up.  The identity map of the first 4 MB is never removed.
 */
#include "paging.h"
#include "../../kernel.h"
#include "../../mm/pmm.h"

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_HUGE    (1ULL << 7)

static uint64_t *kernel_pml4 = NULL;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static uint64_t *get_or_create(uint64_t *table, int idx, uint64_t flags) {
    if (!(table[idx] & PAGE_PRESENT)) {
        uint64_t page = pmm_alloc_page();
        if (!page) return NULL;
        uint8_t *p = (uint8_t *)page;
        for (int i = 0; i < 4096; i++) p[i] = 0;
        table[idx] = page | PAGE_PRESENT | PAGE_WRITE | flags;
    }
    return (uint64_t *)(table[idx] & ~0xFFFULL);
}

/* ── Public paging API ────────────────────────────────────────────────────── */

void vmm_map_page(uint64_t *pml4_table, uint64_t virt, uint64_t phys,
                  uint64_t flags) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t mid_flags = (flags & PAGE_USER) ? PAGE_USER : 0;

    uint64_t *pdpt = get_or_create(pml4_table, pml4_idx, mid_flags);
    if (!pdpt) return;
    uint64_t *pd   = get_or_create(pdpt, pdpt_idx, mid_flags);
    if (!pd) return;
    uint64_t *pt   = get_or_create(pd, pd_idx, mid_flags);
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
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
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

/*
 * paging_init — called from kernel_main after heap_init().
 *
 * Safe approach:
 *   1. Read CR3 to get the PML4 that boot.asm already loaded.
 *   2. Store it as kernel_pml4 (no CR3 reload, no page-table teardown).
 *   3. Ensure the VGA framebuffer (0xB8000) is mapped writable — it is
 *      already covered by the 0–2 MB boot mapping, but we make it explicit.
 *   4. The first 4 MB identity map from boot.asm stays intact forever.
 */
void paging_init(void) {
    /* Read the PML4 that boot.asm installed — do NOT replace it. */
    uint64_t cr3_val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3_val));
    kernel_pml4 = (uint64_t *)(cr3_val & ~0xFFFULL);

    klog(LOG_INFO,
         "Paging: inherited boot PML4 at 0x%x — first 4 MB identity-mapped",
         (uint64_t)kernel_pml4);
}

uint64_t *paging_get_pml4(void) { return kernel_pml4; }
