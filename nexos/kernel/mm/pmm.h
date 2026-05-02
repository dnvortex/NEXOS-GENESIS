/* NexOS — kernel/mm/pmm.h | Physical Memory Manager | MIT License */
#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void     pmm_init(uint64_t mem_lower, uint64_t mem_upper);
void     pmm_init_region(uint64_t base, uint64_t size);
void     pmm_deinit_region(uint64_t base, uint64_t size);
uint64_t pmm_alloc_page(void);
void     pmm_free_page(uint64_t addr);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_frames(void);
void     pmm_print_map(void);   /* log memory map to serial */

#endif
