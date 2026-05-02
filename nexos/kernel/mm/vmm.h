/* NexOS — kernel/mm/vmm.h | Virtual Memory Manager high-level API | MIT License */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define VMM_FLAG_WRITE (1ULL << 1)
#define VMM_FLAG_USER  (1ULL << 2)
#define VMM_FLAG_NX    (1ULL << 63)


/* High-level VMM — all ops use the current kernel PML4 unless noted */
void     vmm_init(void);
void     vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap(uint64_t virt);
uint64_t vmm_phys(uint64_t virt);
uint64_t vmm_create_address_space(void);     /* new PML4, kernel upper-half copied */
void     vmm_switch_address_space(uint64_t pml4_phys); /* write to CR3 */

#endif
