#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

#define GDT_NULL        0
#define GDT_CODE64      1
#define GDT_DATA64      2
#define GDT_USER_CODE   3
#define GDT_USER_DATA   4
#define GDT_TSS         5

#define GDT_ENTRIES     7

#define KERNEL_CS_SELECTOR  ((uint16_t)(GDT_CODE64 * 8))
#define KERNEL_DS_SELECTOR  ((uint16_t)(GDT_DATA64 * 8))
#define USER_CS_SELECTOR    ((uint16_t)((GDT_USER_CODE * 8) | 0x3))
#define USER_DS_SELECTOR    ((uint16_t)((GDT_USER_DATA * 8) | 0x3))

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp0);
extern void gdt_load(struct gdt_ptr *);

#endif
