/* NexOS — kernel/arch/x86_64/paging.h | Virtual memory manager | MIT License */
#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void     paging_init(void);
void     vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap_page(uint64_t *pml4, uint64_t virt);
uint64_t vmm_get_physical(uint64_t *pml4, uint64_t virt);
uint64_t *paging_get_pml4(void);

#define VMM_FLAG_WRITE  (1ULL << 1)
#define VMM_FLAG_USER   (1ULL << 2)

#endif
