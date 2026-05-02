/* NexOS — kernel/mm/heap.c | Free-list kernel heap | MIT License
 *
 * Simple singly-linked free-list allocator.
 * No prev pointer → no backward-merge bug.  Forward-only coalescing is
 * sufficient for a kernel that rarely frees large contiguous regions.
 *
 * Block layout (24 bytes on x86_64):
 *   [0..7 ] size_t  size   — bytes of usable data (header NOT included)
 *   [8    ] uint8_t free   — 1 = free, 0 = used  (+7 pad → align next)
 *   [16..23] heap_block_t *next
 *
 * heap_init zeroes the entire data area so no stale pointer garbage can
 * corrupt the list after the first allocation.
 */
#include "heap.h"
#include "../kernel.h"

typedef struct heap_block {
    size_t           size;   /* usable bytes after header */
    uint8_t          free;   /* 1 = free, 0 = used */
    struct heap_block *next; /* next block (NULL = last) */
} heap_block_t;

static heap_block_t *heap_head = NULL;
static uint8_t      *heap_end  = NULL;

void heap_init(void *start, size_t size) {
    heap_head = (heap_block_t *)start;
    heap_head->size = size - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_end = (uint8_t *)start + size;

    /* Zero the data area: ensures no stale values masquerade as pointers */
    uint8_t *data = (uint8_t *)start + sizeof(heap_block_t);
    for (size_t i = 0; i < heap_head->size; i++) data[i] = 0;

    klog(LOG_INFO, "Heap: 0x%x – 0x%x (%u MB), header=%u B",
         (uint64_t)(uintptr_t)start,
         (uint64_t)(uintptr_t)heap_end,
         (unsigned)(size >> 20),
         (unsigned)sizeof(heap_block_t));
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    /* align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            /* Split only if the remainder can hold a header + ≥8 bytes */
            if (curr->size >= size + sizeof(heap_block_t) + 8) {
                heap_block_t *newb = (heap_block_t *)
                    ((uint8_t *)curr + sizeof(heap_block_t) + size);
                newb->size = curr->size - size - sizeof(heap_block_t);
                newb->free = 1;
                newb->next = curr->next;
                curr->size = size;
                curr->next = newb;
            }
            curr->free = 0;
            return (void *)((uint8_t *)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    klog(LOG_ERROR, "Heap: kmalloc(%u) failed — out of memory", (unsigned)size);
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    block->free = 1;
    /* Forward coalesce only — avoids double-merge corruption from prev ptr */
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next  = block->next->next;
    }
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr)  return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->size >= size) return ptr;
    void *newptr = kmalloc(size);
    if (!newptr) return NULL;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)newptr;
    size_t   n   = block->size < size ? block->size : size;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    kfree(ptr);
    return newptr;
}

void *kmalloc_aligned(size_t size, size_t align) {
    void *ptr = kmalloc(size + align);
    if (!ptr) return NULL;
    uintptr_t addr    = (uintptr_t)ptr;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);
    return (void *)aligned;
}

size_t heap_free_space(void) {
    size_t total = 0;
    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->free) total += curr->size;
        curr = curr->next;
    }
    return total;
}
