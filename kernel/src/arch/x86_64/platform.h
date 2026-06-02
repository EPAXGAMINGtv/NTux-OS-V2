#ifndef NTUX_PLATFORM_H
#define NTUX_PLATFORM_H

#include <mm/hhdm.h>
#include <stdint.h>

static inline uint64_t p2v(uint64_t phys) {
    return phys + hhdm_offset_get();
}

static inline uint64_t v2p(uint64_t virt) {
    return virt - hhdm_offset_get();
}

#endif
