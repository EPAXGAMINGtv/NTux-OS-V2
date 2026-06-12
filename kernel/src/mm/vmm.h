#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "paging.h"

#define KERNEL_HEAP_VADDR     0xFFFFA00000000000ull
#define USER_MMAP_VADDR       0x60000000ull

#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v) (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

void vmm_init(struct limine_memmap_response *memmap);
void* vmm_map_page(void *virt, void *phys, uint64_t flags);
void vmm_unmap_page(void *virt);
void* vmm_get_phys_addr(void *virt);
void* vmm_alloc_page(void);
void* vmm_alloc_pages(size_t count);
void vmm_free_page(void *addr);
void* vmm_alloc_user_pages(size_t count);
void* vmm_alloc_stack(size_t pages);
int vmm_walk_pt(uint64_t pml4_phys, uintptr_t virt, uint64_t *entry_out);
uint64_t vmm_get_kernel_pml4(void);

#endif
