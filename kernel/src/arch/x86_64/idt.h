#ifndef IDT_H
#define IDT_H

#include <stdint.h>




struct idt_entry {
    uint16_t offset_low;     
    uint16_t selector;       
    uint8_t  ist;            
    uint8_t  type_attr;      
    uint16_t offset_mid;     
    uint32_t offset_high;    
    uint32_t zero;           
} __attribute__((packed));




struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));




#define IDT_ENTRIES   256
#define IDT_PRESENT   0x80
#define IDT_RING0     0x00
#define IDT_RING3     0x60
#define IDT_INT_GATE  0x0E
#define IDT_TRAP_GATE 0x0F




void idt_init(void);

extern void idt_load(struct idt_ptr* ptr);  

#endif
