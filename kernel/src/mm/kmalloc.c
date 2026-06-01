#include "kmalloc.h"

#include <drivers/framebuffer/kprint.h>

#define KERNEL_HEAP_POOL_SIZE (32u * 1024u * 1024u)

typedef struct kmalloc_block {
    size_t size;
    struct kmalloc_block *next;
} kmalloc_block_t;

static uint8_t g_heap_pool[KERNEL_HEAP_POOL_SIZE];
static size_t heap_break = 0;
static kmalloc_block_t *free_list = NULL;
static volatile uint8_t g_kmalloc_lock = 0;

static inline size_t align8(size_t size) {
    return (size + 7u) & ~((size_t)7u);
}

static inline uint8_t *block_end(kmalloc_block_t *b) {
    return (uint8_t *)(b + 1) + b->size;
}

void kmalloc_init(void) {
    while (__atomic_test_and_set(&g_kmalloc_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
    heap_break = 0;
    free_list = NULL;
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

    size_t alloc_start = align8(heap_break);
    size_t new_break = alloc_start + sizeof(kmalloc_block_t) + size;
    if (new_break < alloc_start) {
        __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
        return NULL;
    }
    if (new_break > KERNEL_HEAP_POOL_SIZE) {
        kprint("kmalloc: pool exhausted\n");
        __atomic_clear(&g_kmalloc_lock, __ATOMIC_RELEASE);
        return NULL;
    }

    kmalloc_block_t *block = (kmalloc_block_t *)(void *)(g_heap_pool + alloc_start);
    block->size = size;
    heap_break = new_break;
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

    /* Coalesce with next. */
    if (block->next && block_end(block) == (uint8_t *)block->next) {
        block->size += sizeof(kmalloc_block_t) + block->next->size;
        block->next = block->next->next;
    }
    /* Coalesce with previous. */
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
