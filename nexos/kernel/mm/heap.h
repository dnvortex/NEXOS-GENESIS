/* NexOS — kernel/mm/heap.h | Kernel heap allocator | MIT License */
#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

void  heap_init(void *start, size_t size);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t size);
void *kmalloc_aligned(size_t size, size_t align);

#endif
