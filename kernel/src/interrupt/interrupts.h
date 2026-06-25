#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/*
 * Simple interrupt flag manipulation.
 * These wrap the x86 cli/sti instructions to disable/enable interrupts.
 */

/* Enable interrupts (sti) */
void interrupts_enable(void);

/* Disable interrupts (cli) */
void interrupts_disable(void);

/* Returns non-zero if interrupts are currently enabled (RFLAGS.IF) */
int interrupts_are_enabled(void);

#endif
