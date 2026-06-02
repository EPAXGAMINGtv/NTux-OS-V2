#ifndef NTUX_PLATFORM_H
#define NTUX_PLATFORM_H

#include <mm/hhdm.h>
#include <stdint.h>

static inline uint64_t p2v(uint64_t phys) {
    return phys + hhdm_offset_get();
}

static inline uint64_t v2p(uint64_t virt) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t hhdm = hhdm_offset_get();
    cr3 &= ~0xFFF;

    uint64_t* pml4 = (uint64_t*)(hhdm + cr3);
    uint64_t pml4e = pml4[(virt >> 39) & 0x1FF];
    if (!(pml4e & 1)) return 0;

    uint64_t* pdpt = (uint64_t*)(hhdm + (pml4e & ~0xFFF));
    uint64_t pdpte = pdpt[(virt >> 30) & 0x1FF];
    if (!(pdpte & 1)) return 0;
    if (pdpte & (1 << 7))
        return (pdpte & ~((1ULL << 30) - 1)) | (virt & ((1ULL << 30) - 1));

    uint64_t* pd = (uint64_t*)(hhdm + (pdpte & ~0xFFF));
    uint64_t pde = pd[(virt >> 21) & 0x1FF];
    if (!(pde & 1)) return 0;
    if (pde & (1 << 7))
        return (pde & ~((1ULL << 21) - 1)) | (virt & ((1ULL << 21) - 1));

    uint64_t* pt = (uint64_t*)(hhdm + (pde & ~0xFFF));
    uint64_t pte = pt[(virt >> 12) & 0x1FF];
    if (!(pte & 1)) return 0;

    return (pte & ~0xFFF) | (virt & 0xFFF);
}

#endif
