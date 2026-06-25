#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/*
 * ISR (Interrupt Service Routine) handling for CPU exceptions.
 *
 * When the CPU encounters a fault/trap/abort (e.g. page fault, division
 * by zero, general protection fault), it invokes the corresponding IDT
 * entry. These are handled here.
 *
 * For user-space faults, the offending thread is terminated.
 * For kernel-space faults, a blue screen is triggered.
 */

/* Entry point called from isr.asm for all CPU exception handlers */
void isr_handler(void);

/*
 * Handle a CPU exception with an error code.
 * Returns 0 if the fault was handled (thread terminated), never returns
 * for kernel faults (blue screen + HLT).
 */
int isr_handle_error(uint64_t interrupt_number, uint64_t error_code, uint64_t rip, uint64_t cs);

#endif
