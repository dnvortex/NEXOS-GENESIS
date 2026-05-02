/* NexOS — kernel/mm/heap.c | Free-list kernel heap | MIT License */
#include "heap.h"
#include "../kernel.h"

typedef struct block_header {
    size_t size;
    int    free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

#define BLOCK_HDR_SIZE sizeof(block_header_t)

static block_header_t *heap_start = NULL;
static block_header_t *heap_end   = NULL;

void heap_init(void *start, size_t size) {
    heap_start = (block_header_t *)start;
    heap_start->size = size - BLOCK_HDR_SIZE;
    heap_start->free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    heap_end = heap_start;
    klog(LOG_INFO, "Heap: initialized at 0x%p, size=%u KB",
         (uint64_t)(uintptr_t)start, (unsigned)(size / 1024));
}

static void split_block(block_header_t *blk, size_t size) {
    if (blk->size >= size + BLOCK_HDR_SIZE + 16) {
        block_header_t *new_blk = (block_header_t *)((uint8_t *)blk + BLOCK_HDR_SIZE + size);
        new_blk->size = blk->size - size - BLOCK_HDR_SIZE;
        new_blk->free = 1;
        new_blk->next = blk->next;
        new_blk->prev = blk;
        if (blk->next) blk->next->prev = new_blk;
        blk->next = new_blk;
        blk->size = size;
        if (heap_end == blk) heap_end = new_blk;
    }
}

void *kmalloc(size_t size) {
    if (!size) return NULL;
    size = ALIGN_UP(size, 8);

    block_header_t *blk = heap_start;
    while (blk) {
        if (blk->free && blk->size >= size) {
            split_block(blk, size);
            blk->free = 0;
            return (void *)((uint8_t *)blk + BLOCK_HDR_SIZE);
        }
        blk = blk->next;
    }
    klog(LOG_ERROR, "Heap: kmalloc(%u) failed — out of memory", (unsigned)size);
    return NULL;
}

static void merge_free(block_header_t *blk) {
    if (blk->next && blk->next->free) {
        blk->size += BLOCK_HDR_SIZE + blk->next->size;
        if (heap_end == blk->next) heap_end = blk;
        blk->next = blk->next->next;
        if (blk->next) blk->next->prev = blk;
    }
    if (blk->prev && blk->prev->free) {
        blk->prev->size += BLOCK_HDR_SIZE + blk->size;
        blk->prev->next = blk->next;
        if (blk->next) blk->next->prev = blk->prev;
        if (heap_end == blk) heap_end = blk->prev;
    }
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *blk = (block_header_t *)((uint8_t *)ptr - BLOCK_HDR_SIZE);
    blk->free = 1;
    merge_free(blk);
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }

    block_header_t *blk = (block_header_t *)((uint8_t *)ptr - BLOCK_HDR_SIZE);
    if (blk->size >= size) return ptr;

    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    size_t copy_size = blk->size < size ? blk->size : size;
    for (size_t i = 0; i < copy_size; i++) dst[i] = src[i];
    kfree(ptr);
    return new_ptr;
}

void *kmalloc_aligned(size_t size, size_t align) {
    /* Simple approach: allocate extra and align manually */
    void *ptr = kmalloc(size + align);
    if (!ptr) return NULL;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = ALIGN_UP(addr, align);
    return (void *)aligned;
}

size_t heap_free_space(void) {
    size_t total = 0;
    block_header_t *blk = heap_start;
    while (blk) {
        if (blk->free) total += blk->size;
        blk = blk->next;
    }
    return total;
}
