/*
 * isr.c — CPU exception handlers (faults, traps, aborts).
 *
 * Architecture notes:
 * - Some exceptions push an error code onto the stack, some don't.
 * - The assembly wrapper (isr.asm) handles the difference and calls
 *   isr_handle_error() or isr_handle_interrupt() accordingly.
 * - User-space exceptions terminate the thread; kernel exceptions
 *   trigger a blue screen + halt.
 */

#include <interrupt/isr.h>
#include <arch/x86_64/idt.h>
#include <drivers/framebuffer/kprint.h>
#include <sched/thread.h>

/*
 * Emergency exit stack for terminating a faulting user thread.
 * We switch to this stack before calling thread_exit_current() to
 * avoid issues if the thread's kernel stack was corrupted.
 */
#define ISR_EXIT_STACK_SIZE 4096
static uint8_t g_isr_exit_stack[ISR_EXIT_STACK_SIZE];

/*
 * Terminate the current user thread after a CPU fault.
 * Switches to a dedicated emergency stack first.
 */
__attribute__((noreturn)) void isr_terminate_current_thread(void) {
    uintptr_t sp = (uintptr_t)g_isr_exit_stack + ISR_EXIT_STACK_SIZE;
    sp &= ~0xFull;  /* align to 16 bytes */
    __asm__ volatile("mov %0, %%rsp" : : "r"(sp) : "memory");
    thread_exit_current();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Check whether the current thread is a user-space thread */
static int current_thread_is_user(void) {
    uint64_t start = 0;
    uint64_t end = 0;
    if (thread_get_current_user_range(&start, &end) != 0) return 0;
    return (start != 0 && end > start);
}

/*
 * Handle a CPU exception that has an error code on the stack.
 * Interrupts with error codes: #DF(8), #TS(10), #NP(11), #SS(12),
 * #GP(13), #PF(14), #AC(17).
 */
int isr_handle_error(uint64_t interrupt_number, uint64_t error_code, uint64_t rip, uint64_t cs) {
    if (current_thread_is_user()) {
        kprintf("[isr] user fault: int=");
        kprint_uint((uint32_t)interrupt_number);
        kprintf(" err=0x");
        kprint_hex64(error_code);
        kprintf(" rip=0x");
        kprint_hex64(rip);
        kprintf(", terminating thread\n");
        isr_terminate_current_thread();
        return 0;
    }
    trigger_blue_screen(interrupt_number, error_code);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * Handle a CPU exception WITHOUT an error code.
 * Most fault handlers (e.g. #DE(0), #UD(6), #NM(7), #BP(3), #OF(4))
 * fall into this category.
 */
int isr_handle_interrupt(uint64_t interrupt_number, uint64_t rip, uint64_t cs) {
    if (current_thread_is_user()) {
        kprintf("[isr] user interrupt: int=");
        kprint_uint((uint32_t)interrupt_number);
        kprintf(" rip=0x");
        kprint_hex64(rip);
        kprintf(", terminating thread\n");
        isr_terminate_current_thread();
        return 0;
    }
    trigger_blue_screen(interrupt_number, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
