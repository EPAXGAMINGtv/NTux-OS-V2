#include "gdt.h"

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdt_ptr;
static struct tss_entry tss;

#define TSS_ACCESS_BYTE 0x89
#define TSS_FLAGS_BYTE  0x00

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFFu);
    gdt[num].base_middle = (base >> 16) & 0xFFu;
    gdt[num].base_high = (base >> 24) & 0xFFu;
    gdt[num].limit_low = (limit & 0xFFFFu);
    gdt[num].granularity = (limit >> 16) & 0x0Fu;
    gdt[num].granularity |= gran & 0xF0u;
    gdt[num].access = access;
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_set_gate(GDT_NULL, 0, 0, 0x00, 0x00);
    gdt_set_gate(GDT_CODE64, 0, 0, 0x9A, 0x20);
    gdt_set_gate(GDT_DATA64, 0, 0, 0x92, 0x00);
    gdt_set_gate(GDT_USER_CODE, 0, 0, 0xFA, 0x20);
    gdt_set_gate(GDT_USER_DATA, 0, 0, 0xF2, 0x00);

    for (uint64_t i = 0; i < sizeof(tss); ++i) {
        ((uint8_t *)&tss)[i] = 0;
    }

    __asm__ volatile("mov %%rsp, %0" : "=r"(tss.rsp0));
    tss.iomap_base = sizeof(tss);

    const uint64_t tss_base = (uint64_t)&tss;
    const uint32_t tss_limit = (uint32_t)(sizeof(tss) - 1);

    gdt[GDT_TSS].limit_low = tss_limit & 0xFFFFu;
    gdt[GDT_TSS].base_low = tss_base & 0xFFFFu;
    gdt[GDT_TSS].base_middle = (tss_base >> 16) & 0xFFu;
    gdt[GDT_TSS].access = TSS_ACCESS_BYTE;
    gdt[GDT_TSS].granularity = ((tss_limit >> 16) & 0x0Fu) | TSS_FLAGS_BYTE;
    gdt[GDT_TSS].base_high = (tss_base >> 24) & 0xFFu;

    *(uint64_t *)&gdt[GDT_TSS + 1] = tss_base >> 32;

    gdt_load(&gdt_ptr);
    __asm__ volatile("ltr %0" : : "r"((uint16_t)(GDT_TSS * 8)));
}
