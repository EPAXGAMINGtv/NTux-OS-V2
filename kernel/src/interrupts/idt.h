#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// ---------------------------
// NTux-OS: IDT entry (64-bit)
// ---------------------------
struct idt_entry {
    uint16_t offset_low;     // Bits 0–15  of handler address
    uint16_t selector;       // Code segment selector (0x08)
    uint8_t  ist;            // Interrupt Stack Table (0 = none)
    uint8_t  type_attr;      // Gate type + flags
    uint16_t offset_mid;     // Bits 16–31 of handler address
    uint32_t offset_high;    // Bits 32–63 of handler address
    uint32_t zero;           // Reserved
} __attribute__((packed));

// ---------------------------
// NTux-OS: IDTR for lidt
// ---------------------------
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// ---------------------------
// NTux-OS: Constants
// ---------------------------
#define IDT_ENTRIES     256
#define IDT_PRESENT     0x80
#define IDT_RING0       0x00
#define IDT_INT_GATE         0x8E   // 64-bit Interrupt Gate

// ---------------------------
// NTux-OS: Functions
// ---------------------------
void idt_init(void);
static void idt_set_entry(uint8_t index, uint64_t handler);

extern void idt_load(struct idt_ptr* ptr);  // in idt.asm

#endif