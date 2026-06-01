#ifndef ISR_H
#define ISR_H

#include <stdint.h>

void isr_handler(void);
int isr_handle_error(uint64_t interrupt_number, uint64_t error_code, uint64_t rip, uint64_t cs);

#endif
