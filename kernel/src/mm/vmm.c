#include "vmm.h"
#include "pmm.h"
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>

#define KERNEL_HEAP_START 0xE0000000

static vmm_page_table_t* current_page_table = NULL;
static uintptr_t kernel_heap_end = KERNEL_HEAP_START;

void set_pte(pte_t* entry, uintptr_t phys_addr, uint64_t flags) {
    *entry = (phys_addr & ~0xFFF) | (flags & 0xFFF);
}

uintptr_t get_phys_addr_from_pte(pte_t* entry) {
    return *entry & ~0xFFF;
}

void vmm_init(struct limine_memmap_response* memap) {
    kprint("[VMM] Initializing...\n");
    if (!memap || !memap->entries || memap->entry_count == 0) {
        kprint("[VMM] Missing memmap, cannot initialize\n");
        return;
    }
    pmm_init(memap);

    current_page_table = (vmm_page_table_t*)pmm_alloc_page();
    if (!current_page_table) {
        kprint("[VMM] Failed to allocate page-table descriptor\n");
        return;
    }
    current_page_table->entries = (pte_t*)pmm_alloc_page();
    if (!current_page_table->entries) {
        kprint("[VMM] Failed to allocate top-level page table\n");
        current_page_table = NULL;
        return;
    }
    current_page_table->size = 512;

    for (size_t i = 0; i < 512; i++) {
        // Kernel identity map should be supervisor-only.
        set_pte(&current_page_table->entries[i], i * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITABLE);
    }
}

void* vmm_alloc_page(void) {
    void* phys = pmm_alloc_page();
    if (!phys) return NULL;

    uintptr_t virt = kernel_heap_end;
    if (!vmm_map_page((void*)virt, phys, PAGE_PRESENT | PAGE_WRITABLE)) {
        pmm_free_page(phys);
        return NULL;
    }
    kernel_heap_end += PAGE_SIZE;

    return (void*)virt;
}

void vmm_free_page(void* addr) {
    if (!addr) return;
    if (!current_page_table || !current_page_table->entries) return;
    size_t idx = (uintptr_t)addr / PAGE_SIZE;
    if (idx >= current_page_table->size) return;
    pte_t* e = &current_page_table->entries[idx];
    if (!(*e & PAGE_PRESENT)) return;
    uintptr_t phys = get_phys_addr_from_pte(e);
    set_pte(e, 0, 0);
    if (phys) pmm_free_page((void*)phys);
}

void* vmm_map_page(void* virt, void* phys, uint64_t flags) {
    if (!current_page_table || !current_page_table->entries) return NULL;
    size_t idx = (uintptr_t)virt / PAGE_SIZE;
    if (idx >= current_page_table->size) {
        return NULL;
    }
    set_pte(&current_page_table->entries[idx], (uintptr_t)phys, flags);
    return virt;
}

void vmm_unmap_page(void* virt) {
    if (!current_page_table || !current_page_table->entries) return;
    size_t idx = (uintptr_t)virt / PAGE_SIZE;
    if (idx >= current_page_table->size) return;
    set_pte(&current_page_table->entries[idx], 0, 0);
}

void* vmm_alloc_user_pages(size_t count) {
    void* first = NULL;
    uintptr_t start = kernel_heap_end;
    for (size_t i = 0; i < count; i++) {
        void* phys = pmm_alloc_page();
        if (!phys) {
            kernel_heap_end = start;
            return NULL;
        }

        uintptr_t virt = kernel_heap_end;
        if (!vmm_map_page((void*)virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
            pmm_free_page(phys);
            for (size_t j = 0; j < i; ++j) {
                uintptr_t v = start + j * PAGE_SIZE;
                vmm_free_page((void*)v);
            }
            kernel_heap_end = start;
            return NULL;
        }
        kernel_heap_end += PAGE_SIZE;
        if (!first) first = (void*)virt;
    }
    return first;
}

void* vmm_alloc_stack(size_t pages) {
    void* base = vmm_alloc_user_pages(pages);
    if (!base) return NULL;
    return (uint8_t*)base + pages * PAGE_SIZE;
}
