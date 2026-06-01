#include "idt.h"
#include <interrupt/irq.h>
#include <interrupt/isr.h>
#include <interrupt/timer.h>
#include <drivers/framebuffer/kprint.h>
#define IDT_ENTRIES 256

extern uint64_t irq_stub_table[];
extern uint64_t isr_stub_table[];
extern void int80_stub(void);

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idt_descriptor;

static void idt_set_entry(uint8_t index, uint64_t handler) {
    idt[index].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[index].selector    = 0x08;                   
    idt[index].ist         = 0;
    idt[index].type_attr   = IDT_PRESENT | IDT_RING0 | IDT_INT_GATE;
    idt[index].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[index].offset_high = (uint32_t)(handler >> 32);
    idt[index].zero        = 0;
}

static void idt_set_entry_with_attr(uint8_t index, uint64_t handler, uint8_t type_attr) {
    idt[index].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[index].selector    = 0x08;
    idt[index].ist         = 0;
    idt[index].type_attr   = type_attr;
    idt[index].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[index].offset_high = (uint32_t)(handler >> 32);
    idt[index].zero        = 0;
}

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low   = 0;
        idt[i].selector     = 0;
        idt[i].ist          = 0;
        idt[i].type_attr    = 0;
        idt[i].offset_mid   = 0;
        idt[i].offset_high  = 0;
        idt[i].zero         = 0;
    }
    for (int i = 0; i < 32; i++) {
        idt_set_entry(i, isr_stub_table[i]);
    }
    for (int i = 0; i < 16; i++) {
        idt_set_entry(32 + i, irq_stub_table[i]);
    }

    
    idt_set_entry_with_attr(0x80, (uint64_t)(uintptr_t)int80_stub, IDT_PRESENT | IDT_RING3 | IDT_TRAP_GATE);
    
    kprint_ok("Registered IRQ handlers in IDT");

    idt_descriptor.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idt_descriptor.base  = (uint64_t)&idt;

    idt_load(&idt_descriptor);
}
