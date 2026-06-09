#include <interrupt/isr.h>
#include <arch/x86_64/idt.h>
#include <drivers/framebuffer/kprint.h>
#include <sched/thread.h>


#define ISR_EXIT_STACK_SIZE 4096
static uint8_t g_isr_exit_stack[ISR_EXIT_STACK_SIZE];

__attribute__((noreturn)) void isr_terminate_current_thread(void) {
    uintptr_t sp = (uintptr_t)g_isr_exit_stack + ISR_EXIT_STACK_SIZE;
    sp &= ~0xFull;
    __asm__ volatile("mov %0, %%rsp" : : "r"(sp) : "memory");
    thread_exit_current();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static int current_thread_is_user(void) {
    uint64_t start = 0;
    uint64_t end = 0;
    if (thread_get_current_user_range(&start, &end) != 0) return 0;
    return (start != 0 && end > start);
}

int isr_handle_error(uint64_t interrupt_number, uint64_t error_code, uint64_t rip, uint64_t cs) {
    if (current_thread_is_user()) {
        kprintf("[isr] user fault: int="); kprint_uint((uint32_t)interrupt_number); kprintf(" err=0x"); kprint_hex64(error_code); kprintf(" rip=0x"); kprint_hex64(rip); kprintf(", terminating thread\n");
        isr_terminate_current_thread();
        return 0;
    }
    trigger_blue_screen(interrupt_number, error_code);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

int isr_handle_interrupt(uint64_t interrupt_number, uint64_t rip, uint64_t cs) {
    if (current_thread_is_user()) {
        kprintf("[isr] user interrupt: int="); kprint_uint((uint32_t)interrupt_number); kprintf(" rip=0x"); kprint_hex64(rip); kprintf(", terminating thread\n");
        isr_terminate_current_thread();
        return 0;
    }
    trigger_blue_screen(interrupt_number, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
