#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

typedef uint64_t pte_t;

typedef struct vmm_page_table {
    size_t size;
    pte_t *entries;
} vmm_page_table_t;

void vmm_init(struct limine_memmap_response *memap);
void *vmm_alloc_page(void);
void vmm_free_page(void *addr);
void *vmm_map_page(void *virt, void *phys, uint64_t flags);
void vmm_unmap_page(void *virt);
void *vmm_alloc_user_pages(size_t count);
void *vmm_alloc_stack(size_t pages);

/* CR3-aware page table manipulation - works with the currently active page tables */
int vmm_map_current_user_page(uintptr_t virt, uintptr_t phys, uint64_t flags);
void* vmm_alloc_page_at_current(uintptr_t virt, uint64_t flags);

#endif
