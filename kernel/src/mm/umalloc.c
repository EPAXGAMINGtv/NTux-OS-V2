#include "umalloc.h"

#include "pmm.h"
#include "vmm.h"
#include <drivers/framebuffer/kprint.h>

static uintptr_t user_heap_break = USER_HEAP_START;
static uintptr_t user_heap_mapped_end = USER_HEAP_START;
static umalloc_block_t *free_list = NULL;

static inline size_t align8(size_t size) {
    return (size + 7u) & ~((size_t)7u);
}

static int expand_user_heap_to(uintptr_t required_end) {
    while (user_heap_mapped_end < required_end) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            kprint("umalloc: out of physical memory\n");
            return 0;
        }
        if (!vmm_map_page((void *)user_heap_mapped_end, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
            pmm_free_page(phys);
            kprint("umalloc: vmm_map_page failed\n");
            return 0;
        }
        user_heap_mapped_end += PAGE_SIZE;
    }
    return 1;
}

void umalloc_init(void) {
    user_heap_break = USER_HEAP_START;
    user_heap_mapped_end = USER_HEAP_START;
    free_list = NULL;
}

void *umalloc(size_t size) {
    if (size == 0) return NULL;
    size = align8(size);

    umalloc_block_t **prev = &free_list;
    umalloc_block_t *curr = free_list;
    while (curr) {
        if (curr->size >= size) {
            *prev = curr->next;
            return (void *)(curr + 1);
        }
        prev = &curr->next;
        curr = curr->next;
    }

    uintptr_t alloc_start = (user_heap_break + 7u) & ~(uintptr_t)7u;
    uintptr_t new_break = alloc_start + sizeof(umalloc_block_t) + size;
    if (new_break < alloc_start) return NULL;

    if (!expand_user_heap_to(new_break)) return NULL;

    umalloc_block_t *block = (umalloc_block_t *)alloc_start;
    block->size = size;
    user_heap_break = new_break;
    return (void *)(block + 1);
}

void ufree(void *ptr) {
    if (!ptr) return;

    umalloc_block_t *block = (umalloc_block_t *)ptr - 1;
    block->next = free_list;
    free_list = block;
}
