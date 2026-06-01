#include <stdint.h>
#include "syscall.h"
#include <arch/x86_64/msr.h>
#include <arch/x86_64/gdt.h>

#define IA32_STAR   0xC0000081
#define IA32_LSTAR  0xC0000082
#define IA32_FMASK  0xC0000084

extern void syscall_entry(void);

void syscall_init(void) {
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry);

    /* STAR[47:32]=kernel CS, STAR[63:48]=user CS base selector for SYSRET. */
    uint64_t star =
        ((uint64_t)KERNEL_CS_SELECTOR << 32) |
        ((uint64_t)(USER_CS_SELECTOR - 0x10u) << 48);

    wrmsr(IA32_STAR, star);

    wrmsr(IA32_FMASK, 0);
}
