#include <stdint.h>
#include "lwip/opt.h"
#include "lwip/arch.h"

extern uint64_t get_tick_count(void);
extern uint32_t timer_get_hz(void);

uint32_t sys_now(void) {
    uint64_t ticks = get_tick_count();
    uint32_t hz = timer_get_hz();
    if (hz == 0) hz = 1000;
    return (uint32_t)(ticks * 1000 / hz);
}

sys_prot_t sys_arch_protect(void) {
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r"(flags)
        :
        : "memory"
    );
    return (sys_prot_t)flags;
}

void sys_arch_unprotect(sys_prot_t pval) {
    if (pval & 0x200) {
        __asm__ volatile("sti");
    }
}


