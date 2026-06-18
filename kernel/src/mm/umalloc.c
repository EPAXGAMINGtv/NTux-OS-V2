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
            size_t remaining = curr->size - size;
            if (remaining >= sizeof(umalloc_block_t) + 8u) {
                umalloc_block_t *split = (umalloc_block_t *)((uint8_t *)(curr + 1) + size);
                split->size = remaining - sizeof(umalloc_block_t);
                split->next = curr->next;
                *prev = split;
                curr->size = size;
            } else {
                *prev = curr->next;
            }
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

static inline uint8_t *ublock_end(umalloc_block_t *b) {
    return (uint8_t *)(b + 1) + b->size;
}

void ufree(void *ptr) {
    if (!ptr) return;

    umalloc_block_t *block = (umalloc_block_t *)ptr - 1;
    umalloc_block_t **prev = &free_list;
    while (*prev && *prev < block) {
        prev = &(*prev)->next;
    }
    block->next = *prev;
    *prev = block;

    if (block->next && ublock_end(block) == (uint8_t *)block->next) {
        block->size += sizeof(umalloc_block_t) + block->next->size;
        block->next = block->next->next;
    }
    if (prev != &free_list) {
        umalloc_block_t *p = free_list;
        while (p && p->next != block) p = p->next;
        if (p && ublock_end(p) == (uint8_t *)block) {
            p->size += sizeof(umalloc_block_t) + block->size;
            p->next = block->next;
        }
    }

    {
        umalloc_block_t *last = free_list;
        while (last && last->next) last = last->next;
        if (last && ublock_end(last) == (uint8_t *)user_heap_mapped_end) {
            uintptr_t blk_start = (uintptr_t)last;
            uintptr_t new_mapped = blk_start & ~(uintptr_t)(PAGE_SIZE - 1u);
            if (new_mapped >= USER_HEAP_START && new_mapped < user_heap_mapped_end) {
                for (uintptr_t addr = new_mapped; addr < user_heap_mapped_end; addr += PAGE_SIZE) {
                    void *phys = vmm_get_phys_addr((void *)addr);
                    vmm_unmap_page((void *)addr);
                    if (phys) pmm_free_page(phys);
                }
                user_heap_mapped_end = new_mapped;
                if (free_list == last) {
                    free_list = last->next;
                } else {
                    umalloc_block_t *p = free_list;
                    while (p && p->next != last) p = p->next;
                    if (p) p->next = last->next;
                }
                if (user_heap_break > user_heap_mapped_end)
                    user_heap_break = user_heap_mapped_end;
            }
        }
    }
}
