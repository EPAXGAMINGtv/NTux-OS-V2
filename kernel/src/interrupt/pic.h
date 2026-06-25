#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/*
 * Legacy 8259A PIC (Programmable Interrupt Controller).
 *
 * The PIC is the original IBM PC/AT interrupt controller.
 * It manages 16 IRQs (8 master + 8 slave chained via IRQ 2).
 *
 * Modern systems use the APIC instead, but the PIC is still present
 * on most x86 hardware and must be properly initialized (or disabled)
 * to avoid spurious interrupts.
 *
 * I/O ports:
 *   Master:  CMD=0x20, DATA=0x21
 *   Slave:   CMD=0xA0, DATA=0xA1
 */

/* PIC I/O ports */
#define MASTER_PIC_CMD    0x20
#define MASTER_PIC_DATA   0x21
#define SLAVE_PIC_CMD     0xA0
#define SLAVE_PIC_DATA    0xA1

/* ICW1 (Initialization Command Word 1) bits */
#define PIC_ICW1_ICW4     0x01   /* Expect ICW4 */
#define PIC_ICW1_SINGLE   0x02   /* Single mode (no slave) */
#define PIC_ICW1_INTERVAL4 0x04  /* Call address interval = 4 */
#define PIC_ICW1_LEVEL    0x08   /* Level-triggered mode */

/* ICW4 bits */
#define PIC_ICW4_8086     0x01   /* 8086/8088 mode */
#define PIC_ICW4_MASTER   0x02   /* Auto EOI for master */
#define PIC_ICW4_SLAVE    0x01   /* Auto EOI for slave */

/* Initialize the PICs with default vector offsets */
void pic_init(void);

/* Send End-Of-Interrupt signal for a given IRQ */
void pic_send_eoi(uint8_t irq);

/* Mask (disable) a specific IRQ line */
void pic_set_mask(uint8_t irq_line);

/* Unmask (enable) a specific IRQ line */
void pic_clear_mask(uint8_t irq_line);

#endif
