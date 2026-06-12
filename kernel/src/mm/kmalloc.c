#include "kmalloc.h"
#include "vmm.h"
#include "paging.h"
#include <drivers/framebuffer/kprint.h>

#define HEAP_CHUNK_PAGES 16

typedef struct kmalloc_block {
    size_t size;
    struct kmalloc_block *next;
} kmalloc_block_t;

static kmalloc_block_t *free_list = NULL;
static uintptr_t heap_cur = 0;
static uintptr_t heap_end = 0;
static volatile uint8_t g_kmalloc_lock = 0;

static inline size_t align8(size_t size) {
    return (size + 7u) & ~((size_t)7u);
}

static inline uint8_t *block_end(kmalloc_block_t *b) {
    return (uint8_t *)(b + 1) + b->size;
}

static int grow_heap(size_t min_bytes) {
    size_t pages_needed = (min_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t pages = pages_needed > HEAP_CHUNK_PAGES ? pages_needed : HEAP_CHUNK_PAGES;
    void *chunk = vmm_alloc_pages(pages);
    if (!chunk) return 0;
    heap_cur = (uintptr_t)chunk;
    heap_end = heap_cur + pages * PAGE_SIZE;
    return 1;
}

void kmalloc_init(void) {
    while (__atomic_test_and_set(&g_kmalloc_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
    free_list = NULL;
    heap_cur = 0;
    heap_end = 0;
    __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = align8(size);
    while (__atomic_test_and_set(&g_kmalloc_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }

    kmalloc_block_t **prev = &free_list;
    kmalloc_block_t *curr = free_list;
    while (curr) {
        if (curr->size >= size) {
            size_t remaining = curr->size - size;
            if (remaining >= sizeof(kmalloc_block_t) + 8u) {
                kmalloc_block_t *split = (kmalloc_block_t *)((uint8_t *)(curr + 1) + size);
                split->size = remaining - sizeof(kmalloc_block_t);
                split->next = curr->next;
                *prev = split;
                curr->size = size;
            } else {
                *prev = curr->next;
            }
            __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
            return (void *)(curr + 1);
        }
        prev = &curr->next;
        curr = curr->next;
    }

    size_t alloc_start = align8(heap_cur);
    size_t need = alloc_start + sizeof(kmalloc_block_t) + size;
    if (need > heap_end) {
        if (!grow_heap(sizeof(kmalloc_block_t) + size)) {
            kprint("kmalloc: out of memory\n");
            __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
            return NULL;
        }
        alloc_start = align8(heap_cur);
        need = alloc_start + sizeof(kmalloc_block_t) + size;
        if (need > heap_end) {
            kprint("kmalloc: heap growth failed (fragmentation?)\n");
            __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
            return NULL;
        }
    }

    kmalloc_block_t *block = (kmalloc_block_t *)alloc_start;
    block->size = size;
    heap_cur = need;
    __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
    return (void *)(block + 1);
}

void kfree(void *ptr) {
    if (!ptr) return;
    while (__atomic_test_and_set(&g_kmalloc_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }

    kmalloc_block_t *block = (kmalloc_block_t *)ptr - 1;
    kmalloc_block_t **prev = &free_list;
    while (*prev && *prev < block) {
        prev = &(*prev)->next;
    }
    block->next = *prev;
    *prev = block;

    if (block->next && block_end(block) == (uint8_t *)block->next) {
        block->size += sizeof(kmalloc_block_t) + block->next->size;
        block->next = block->next->next;
    }
    if (prev != &free_list) {
        kmalloc_block_t *p = free_list;
        while (p && p->next != block) p = p->next;
        if (p && block_end(p) == (uint8_t *)block) {
            p->size += sizeof(kmalloc_block_t) + block->size;
            p->next = block->next;
        }
    }
    __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
}
