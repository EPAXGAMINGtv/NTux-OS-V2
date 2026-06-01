#include "vmm.h"
#include "pmm.h"
#include "hhdm.h"
#include "paging.h"
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>

#define KERNEL_HEAP_START 0xE0000000

static vmm_page_table_t* current_page_table = NULL;
static uintptr_t kernel_heap_end = KERNEL_HEAP_START;

static inline uint64_t read_cr3(void) {
    uint64_t v = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void* phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    return (void*)(off ? (phys + (uintptr_t)off) : phys);
}

int vmm_map_current_user_page(uintptr_t virt, uintptr_t phys, uint64_t flags) {
    const uint64_t pml4_i = (virt >> 39) & 0x1FFu;
    const uint64_t pdpt_i = (virt >> 30) & 0x1FFu;
    const uint64_t pd_i   = (virt >> 21) & 0x1FFu;
    const uint64_t pt_i   = (virt >> 12) & 0x1FFu;

    uint64_t pml4_phys = read_cr3() & ~0xFFFull;
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    uint64_t e = pml4[pml4_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pdpt = pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset(phys_to_virt((uintptr_t)new_pdpt), 0, PAGE_SIZE);
        pml4[pml4_i] = ((uint64_t)(uintptr_t)new_pdpt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | ((flags) & PAGE_USER);
        e = pml4[pml4_i];
    } else {
        pml4[pml4_i] = e | PAGE_USER | PAGE_WRITABLE;
    }

    uint64_t* pdpt = (uint64_t*)phys_to_virt(e & ~0xFFFull);
    e = pdpt[pdpt_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pd = pmm_alloc_page();
        if (!new_pd) return -1;
        memset(phys_to_virt((uintptr_t)new_pd), 0, PAGE_SIZE);
        pdpt[pdpt_i] = ((uint64_t)(uintptr_t)new_pd & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | ((flags) & PAGE_USER);
        e = pdpt[pdpt_i];
    } else {
        pdpt[pdpt_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) return 0;

    uint64_t* pd = (uint64_t*)phys_to_virt(e & ~0xFFFull);
    e = pd[pd_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pt = pmm_alloc_page();
        if (!new_pt) return -1;
        memset(phys_to_virt((uintptr_t)new_pt), 0, PAGE_SIZE);
        pd[pd_i] = ((uint64_t)(uintptr_t)new_pt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | ((flags) & PAGE_USER);
        e = pd[pd_i];
    } else {
        pd[pd_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) return 0;

    uint64_t* pt = (uint64_t*)phys_to_virt(e & ~0xFFFull);
    e = pt[pt_i];
    if ((e & PAGE_PRESENT) == 0) {
        pt[pt_i] = (phys & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | ((flags) & PAGE_USER);
    } else {
        pt[pt_i] = (phys & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    return 0;
}

void* vmm_alloc_page_at_current(uintptr_t virt, uint64_t flags) {
    void* phys = pmm_alloc_page();
    if (!phys) return NULL;
    memset(phys_to_virt((uintptr_t)phys), 0, PAGE_SIZE);
    if (vmm_map_current_user_page(virt, (uintptr_t)phys, flags) != 0) {
        pmm_free_page(phys);
        return NULL;
    }
    return (void*)virt;
}

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
        uintptr_t virt = kernel_heap_end;
        void* phys = pmm_alloc_page();
        if (!phys) {
            kernel_heap_end = start;
            return NULL;
        }
        memset(phys_to_virt((uintptr_t)phys), 0, PAGE_SIZE);

        if (vmm_map_current_user_page(virt, (uintptr_t)phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page(phys);
            for (size_t j = 0; j < i; ++j) {
                uintptr_t v = start + j * PAGE_SIZE;
                uint64_t* pt = NULL;
                uint64_t pml4_phys = read_cr3() & ~0xFFFull;
                uint64_t e = ((uint64_t*)phys_to_virt(pml4_phys))[(v >> 39) & 0x1FFu];
                if (e & PAGE_PRESENT) {
                    uint64_t* pdpt = (uint64_t*)phys_to_virt(e & ~0xFFFull);
                    e = pdpt[(v >> 30) & 0x1FFu];
                    if (e & PAGE_PRESENT && !(e & PAGE_HUGE)) {
                        uint64_t* pd = (uint64_t*)phys_to_virt(e & ~0xFFFull);
                        e = pd[(v >> 21) & 0x1FFu];
                        if (e & PAGE_PRESENT && !(e & PAGE_HUGE)) {
                            pt = (uint64_t*)phys_to_virt(e & ~0xFFFull);
                            uintptr_t p = pt[(v >> 12) & 0x1FFu] & ~0xFFFull;
                            pt[(v >> 12) & 0x1FFu] = 0;
                            if (p) pmm_free_page((void*)(uintptr_t)p);
                        }
                    }
                }
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
