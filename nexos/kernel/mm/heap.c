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
 * Optimisations applied:
 *   - heap_init: zero the data region with 8-byte word writes (8× faster).
 *   - krealloc:  copy bytes with 8-byte word writes (8× faster).
 *   - kmalloc_aligned: stores the raw base pointer in the word immediately
 *     before the aligned return address so kfree_aligned() can recover it.
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

    /* Zero the data area with 8-byte word writes — 8× faster than byte loop */
    uint64_t *data64 = (uint64_t *)((uint8_t *)start + sizeof(heap_block_t));
    size_t    words  = heap_head->size / 8;
    for (size_t i = 0; i < words; i++) data64[i] = 0;
    /* Trailing bytes (unlikely: heap size is always a multiple of 8) */
    uint8_t *tail = (uint8_t *)(data64 + words);
    size_t   rem  = heap_head->size & 7;
    for (size_t i = 0; i < rem; i++) tail[i] = 0;

    klog(LOG_INFO, "Heap: 0x%x - 0x%x (%u MB), header=%u B",
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

    klog(LOG_ERROR, "Heap: kmalloc(%u) failed - out of memory", (unsigned)size);
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

    /* Copy with 8-byte word writes — 8× faster than byte loop */
    size_t n     = block->size < size ? block->size : size;
    size_t words = n / 8;
    const uint64_t *s64 = (const uint64_t *)ptr;
    uint64_t       *d64 = (uint64_t *)newptr;
    for (size_t i = 0; i < words; i++) d64[i] = s64[i];
    /* Trailing bytes */
    const uint8_t *sb = (const uint8_t *)(s64 + words);
    uint8_t       *db = (uint8_t       *)(d64 + words);
    size_t rem = n & 7;
    for (size_t i = 0; i < rem; i++) db[i] = sb[i];

    kfree(ptr);
    return newptr;
}

/* kmalloc_aligned — returns a pointer aligned to `align` bytes (must be a
 * power of two).  The original base pointer is stored in the sizeof(void*)
 * slot immediately before the returned pointer so kfree_aligned() can
 * recover it.  Never pass an aligned pointer to plain kfree(). */
void *kmalloc_aligned(size_t size, size_t align) {
    if (!align || (align & (align - 1))) return NULL; /* must be power-of-2 */
    void *raw = kmalloc(size + align + sizeof(void *));
    if (!raw) return NULL;
    uintptr_t base    = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + align - 1) & ~(align - 1);
    ((void **)aligned)[-1] = raw;   /* store original pointer for kfree_aligned */
    return (void *)aligned;
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    kfree(((void **)ptr)[-1]);
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
