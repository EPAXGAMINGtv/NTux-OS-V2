#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/*
 * IRQ (Interrupt Request) management.
 *
 * IRQs are hardware interrupts from devices (timer, keyboard, disk, etc.).
 * The legacy PIC (8259A) supports 16 IRQs (0-15), mapped to IDT vectors
 * 32-47 by default. When using the I/O APIC, these become GSIs (Global
 * System Interrupts) which can be routed to any vector.
 *
 * Each IRQ can have one registered handler function.
 */

/* Register a handler for a given IRQ (0-15) */
void irq_register_handler(int irq, void (*handler)(void));

/* C-level IRQ handler dispatcher (called from irq.asm) */
void irq_handler_c(uint64_t *stack);

#endif
