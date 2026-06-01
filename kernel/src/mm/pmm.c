#include "pmm.h"
#include "hhdm.h"

static uint8_t *bitmap = 0;
static size_t bitmap_size = 0;
static size_t total_pages = 0;
static size_t free_pages = 0;
static uintptr_t memory_start = 0;
static size_t total_usable_bytes = 0;
static size_t last_alloc_page = 1;

static inline uintptr_t phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    if (off) return phys + (uintptr_t)off;
    return phys;
}

static inline void set_bit(size_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void clear_bit(size_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int test_bit(size_t bit) {
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

void pmm_init(struct limine_memmap_response *memmap) {
    if (!memmap || memmap->entry_count == 0 || !memmap->entries) {
        return;
    }
    total_usable_bytes = 0;
    size_t highest_addr = 0;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == USABLE_MEMORY) {
            total_usable_bytes += entry->length;
        }
        uintptr_t end = entry->base + entry->length;
        if (end > highest_addr) {
            highest_addr = end;
        }
    }

    total_pages = highest_addr / PAGE_SIZE;
    bitmap_size = total_pages / 8 + 1;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == USABLE_MEMORY && entry->length >= bitmap_size) {
            uintptr_t bitmap_phys = (uintptr_t)entry->base;
            bitmap = (uint8_t *)phys_to_virt(bitmap_phys);
            entry->base += bitmap_size;
            entry->length -= bitmap_size;
            break;
        }
    }

    if (!bitmap) {
        return;
    }

    for (size_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == USABLE_MEMORY) {
            uintptr_t addr = entry->base;
            while (addr + PAGE_SIZE <= entry->base + entry->length) {
                size_t page = addr / PAGE_SIZE;
                clear_bit(page);
                free_pages++;
                addr += PAGE_SIZE;
            }
        }
    }

    memory_start = (uintptr_t)bitmap + bitmap_size;
    // Never hand out the zero page (NULL).
    if (!test_bit(0)) {
        set_bit(0);
        if (free_pages > 0) free_pages--;
    }
    last_alloc_page = 1;
}

void *pmm_alloc_page(void) {
    if (!bitmap || total_pages == 0) return 0;
    size_t start = last_alloc_page;
    size_t word_count = (total_pages + 63u) / 64u;
    uint64_t *words = (uint64_t *)(void *)bitmap;

    for (size_t pass = 0; pass < 2; ++pass) {
        size_t start_word = (pass == 0) ? (start / 64u) : 0u;
        for (size_t w = start_word; w < word_count; ++w) {
            uint64_t word = words[w];
            if (word != ~0ull) {
                uint64_t free_mask = ~word;
                unsigned bit = (unsigned)__builtin_ctzll(free_mask);
                size_t page = w * 64u + (size_t)bit;
                if (page == 0 || page >= total_pages) continue;
                if (!test_bit(page)) {
                    set_bit(page);
                    free_pages--;
                    last_alloc_page = page + 1;
                    if (last_alloc_page >= total_pages) last_alloc_page = 1;
                    return (void *)(page * PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_page(void *addr) {
    size_t page = (uintptr_t)addr / PAGE_SIZE;
    if (page == 0 || page >= total_pages) return;
    if (test_bit(page)) {
        clear_bit(page);
        free_pages++;
    }
}

size_t pmm_get_free_memory(void) {
    return free_pages * PAGE_SIZE;
}

size_t pmm_get_total_usable_memory(void) {
    return total_usable_bytes;
}

uint32_t pmm_get_total_memory_32(struct limine_memmap_response *memmap) {
    uint64_t total = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total += entry->length;
        }
    }


    return (uint32_t)total;
}
