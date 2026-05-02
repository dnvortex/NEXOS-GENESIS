/* NexOS — kernel/mm/heap.h | Kernel heap allocator | MIT License */
#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

/*
 * Fixed heap region.
 * boot.asm identity-maps 32 MB (16×2MB huge pages), so the heap window
 * 0x1200000–0x1A00000 (18–26 MB) is always accessible before paging_init.
 * The static-BSS approach grew kernel_end to ~17.5 MB and could confuse
 * GRUB's ELF BSS zeroing; this fixed-address scheme avoids that entirely.
 */
#define HEAP_START  0x1200000UL            /* physical 18 MB */
#define HEAP_SIZE   (8U * 1024U * 1024U)  /* 8 MB  */

void   heap_init(void *start, size_t size);
void  *kmalloc(size_t size);
void   kfree(void *ptr);
void  *krealloc(void *ptr, size_t size);
void  *kmalloc_aligned(size_t size, size_t align);
size_t heap_free_space(void);   /* walk free-list, return total free bytes */

#endif
