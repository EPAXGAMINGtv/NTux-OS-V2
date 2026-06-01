#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "paging.h"

typedef uint64_t pte_t;

typedef struct vmm_page_table {
    pte_t* entries;
    size_t size;
} vmm_page_table_t;

void vmm_init(struct limine_memmap_response* memmap);
void* vmm_alloc_page(void);
void vmm_free_page(void* addr);
void* vmm_map_page(void* virt, void* phys, uint64_t flags);
void vmm_unmap_page(void* virt);
void* vmm_alloc_user_pages(size_t count);
void* vmm_alloc_stack(size_t pages);

void set_pte(pte_t* entry, uintptr_t phys_addr, uint64_t flags);
uintptr_t get_phys_addr_from_pte(pte_t* entry);

#endif
