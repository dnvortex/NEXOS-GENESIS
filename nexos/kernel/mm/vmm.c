/* NexOS — kernel/mm/vmm.c | Virtual Memory Manager | MIT License */
#include "vmm.h"
#include "pmm.h"
#include "../kernel.h"
#include "../arch/x86_64/paging.h"

void vmm_init(void) {
    klog(LOG_INFO, "VMM: initialized - 4-level paging active");
}

/* Map a physical page to a virtual address in the kernel PML4 */
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    vmm_map_page(paging_get_pml4(), virt, phys, flags);
}

/* Remove a mapping and flush TLB */
void vmm_unmap(uint64_t virt) {
    vmm_unmap_page(paging_get_pml4(), virt);
}

/* Walk the page table and return physical address for virt, or 0 if not mapped */
uint64_t vmm_phys(uint64_t virt) {
    return vmm_get_physical(paging_get_pml4(), virt);
}

/*
 * Allocate a new PML4, copy kernel upper-half mappings (entries 256-511),
 * and return the physical address of the new PML4.
 */
uint64_t vmm_create_address_space(void) {
    uint64_t new_phys = pmm_alloc_page();
    if (!new_phys) return 0;

    uint64_t *new_pml4  = (uint64_t *)new_phys;
    uint64_t *kern_pml4 = paging_get_pml4();

    /* Zero lower half (user space) */
    for (int i = 0;   i < 256; i++) new_pml4[i] = 0;
    /* Share upper half (kernel mappings) */
    for (int i = 256; i < 512; i++) new_pml4[i] = kern_pml4[i];

    return new_phys;
}

/* Switch to a different address space by writing pml4_phys to CR3 */
void vmm_switch_address_space(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}
