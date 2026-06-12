#include "vmm.h"
#include "pmm.h"
#include "hhdm.h"
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>

static uint64_t kernel_pml4_phys = 0;
static uintptr_t kernel_heap_cur = KERNEL_HEAP_VADDR;
static uintptr_t user_mmap_cur = USER_MMAP_VADDR;

static uintptr_t phys_to_virt_addr(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    return off ? (phys + (uintptr_t)off) : phys;
}

static int walk_pt(uint64_t pml4_phys, uintptr_t virt, int create,
                   uint64_t *entry_out, uintptr_t *table_out,
                   uint64_t extra_flags) {
    size_t idx[4] = {
        PML4_IDX(virt),
        PDPT_IDX(virt),
        PD_IDX(virt),
        PT_IDX(virt)
    };

    uint64_t tbl_phys_ = pml4_phys;

    for (int level = 3; ; level--) {
        uint64_t *table = (uint64_t *)phys_to_virt_addr(tbl_phys_);
        uint64_t entry = table[idx[3u - (size_t)level]];

        if (level == 0) {
            if (entry_out) *entry_out = entry;
            if (table_out) *table_out = tbl_phys_;
            return (entry & PAGE_PRESENT) ? 1 : 0;
        }

        if (!(entry & PAGE_PRESENT)) {
            if (!create) return 0;
            void *new_page = pmm_alloc_page();
            if (!new_page) return -1;
            memset((void *)phys_to_virt_addr((uintptr_t)new_page), 0, PAGE_SIZE);
            entry = (uintptr_t)new_page | PAGE_PRESENT | PAGE_WRITABLE | extra_flags;
            table[idx[3u - (size_t)level]] = entry;
        } else if (extra_flags) {
            table[idx[3u - (size_t)level]] = entry | extra_flags;
        }

        tbl_phys_ = entry & ~0xFFFull;
    }
}

uint64_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}

void vmm_init(struct limine_memmap_response *memmap) {
    kprint("[VMM] Initializing proper 4-level paging...\n");
    if (!memmap || !memmap->entries || memmap->entry_count == 0) {
        kprint("[VMM] Missing memmap, cannot initialize\n");
        return;
    }

    pmm_init(memmap);

    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_pml4_phys));
    kernel_pml4_phys &= ~0xFFFull;

    kprint("[VMM] Kernel PML4 initialized\n");
}

void* vmm_map_page(void *virt, void *phys, uint64_t flags) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFull;

    uint64_t extra = flags & (PAGE_USER);

    uintptr_t pt_phys = 0;
    uint64_t old = 0;
    int ret = walk_pt(cr3, (uintptr_t)virt, 1, &old, &pt_phys, extra);
    if (ret < 0) return NULL;

    uint64_t *pt = (uint64_t *)phys_to_virt_addr(pt_phys);
    pt[PT_IDX((uintptr_t)virt)] = ((uintptr_t)phys & ~0xFFFull) | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return virt;
}

void vmm_unmap_page(void *virt) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFull;

    uintptr_t pt_phys = 0;
    uint64_t old = 0;
    int ret = walk_pt(cr3, (uintptr_t)virt, 0, &old, &pt_phys, 0);
    if (ret <= 0) return;

    uint64_t *pt = (uint64_t *)phys_to_virt_addr(pt_phys);
    pt[PT_IDX((uintptr_t)virt)] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void* vmm_get_phys_addr(void *virt) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFull;

    uint64_t entry = 0;
    int ret = walk_pt(cr3, (uintptr_t)virt, 0, &entry, NULL, 0);
    if (ret <= 0) return NULL;

    uintptr_t offset = (uintptr_t)virt & 0xFFF;
    return (void *)((entry & ~0xFFFull) | offset);
}

void* vmm_alloc_page(void) {
    return vmm_alloc_pages(1);
}

void* vmm_alloc_pages(size_t count) {
    uintptr_t start = kernel_heap_cur;
    uintptr_t virt = start;

    for (size_t i = 0; i < count; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            for (size_t j = 0; j < i; j++) {
                uintptr_t v = start + j * PAGE_SIZE;
                vmm_unmap_page((void *)v);
            }
            return NULL;
        }
        if (!vmm_map_page((void *)virt, phys, PAGE_PRESENT | PAGE_WRITABLE)) {
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uintptr_t v = start + j * PAGE_SIZE;
                vmm_unmap_page((void *)v);
            }
            return NULL;
        }
        virt += PAGE_SIZE;
    }

    kernel_heap_cur = virt;
    return (void *)start;
}

void vmm_free_page(void *addr) {
    if (!addr) return;
    void *phys = vmm_get_phys_addr(addr);
    if (phys) {
        vmm_unmap_page(addr);
        pmm_free_page(phys);
    }
}

void* vmm_alloc_user_pages(size_t count) {
    uintptr_t start = user_mmap_cur;
    uintptr_t virt = start;
    uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    for (size_t i = 0; i < count; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            for (size_t j = 0; j < i; j++) {
                uintptr_t v = start + j * PAGE_SIZE;
                vmm_unmap_page((void *)v);
            }
            return NULL;
        }
        if (!vmm_map_page((void *)virt, phys, flags)) {
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uintptr_t v = start + j * PAGE_SIZE;
                vmm_unmap_page((void *)v);
            }
            return NULL;
        }
        virt += PAGE_SIZE;
    }

    user_mmap_cur = virt;
    return (void *)start;
}

void* vmm_alloc_stack(size_t pages) {
    void *base = vmm_alloc_user_pages(pages);
    if (!base) return NULL;
    return (uint8_t *)base + pages * PAGE_SIZE;
}

int vmm_walk_pt(uint64_t pml4_phys, uintptr_t virt, uint64_t *entry_out) {
    return walk_pt(pml4_phys, virt, 0, entry_out, NULL, 0);
}
