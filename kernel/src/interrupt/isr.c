#include <interrupt/isr.h>
#include <arch/x86_64/idt.h>
#include <drivers/framebuffer/kprint.h>
#include <sched/thread.h>
#include <mm/vmm.h>

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

static void log_error_code_breakdown(uint64_t error_code) {
    kprint("  Error Code Breakdown:\n");
    kprint("    Bit 0 (Protection): ");
    kprint((error_code & 1) ? "YES (protection violation)\n" : "No (non-present page)\n");
    kprint("    Bit 1 (Access Type): ");
    kprint((error_code & 2) ? "Write\n" : "Read\n");
    kprint("    Bit 2 (CPL): ");
    kprint((error_code & 4) ? "User mode\n" : "Supervisor mode\n");
    kprint("    Bit 3 (Reserved): ");
    kprint((error_code & 8) ? "YES (reserved bit set)\n" : "No\n");
    kprint("    Bit 4 (IFetch): ");
    kprint((error_code & 16) ? "Instruction fetch\n" : "Data access\n");
}

static void log_cr2(void) {
    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    kprint("  Faulting Address (CR2): 0x");
    kprint_hex64(cr2);
    kprint("\n");
}

static void log_memory_region(uint64_t addr) {
    if (!addr) return;
    int tid = thread_get_current_tid();
    if (tid < 0) return;
    uint64_t start = 0, end = 0;
    if (thread_get_current_user_range(&start, &end) != 0) return;
    if (start == 0 || end == 0) return;
    kprint("  Process Memory Range: 0x");
    kprint_hex64(start);
    kprint(" - 0x");
    kprint_hex64(end);
    kprint("\n");
    if (addr >= start && addr < end) {
        kprint("  -> Address IS within process memory\n");
    } else {
        kprint("  -> Address OUTSIDE process memory\n");
    }
}

void isr_print_stack_trace(uint64_t rbp, uint64_t rip) {
    kprint("  Stack Trace:\n");
    kprint("    [0] RIP=0x");
    kprint_hex64(rip);
    kprint("\n");
    for (int i = 1; i < 16; ++i) {
        if (rbp == 0 || rbp == (uint64_t)-1) break;
        if (rbp < 0x1000) break;
        uint64_t next_rbp = 0;
        uint64_t next_rip = 0;
        volatile uint64_t* frame = (volatile uint64_t*)rbp;
        __asm__ volatile("" : : : "memory");
        next_rbp = frame[0];
        next_rip = frame[1];
        if (next_rip == 0 || next_rip == (uint64_t)-1) break;
        if (next_rip < 0x1000) break;
        kprint("    [");
        kprint_int(i);
        kprint("] RIP=0x");
        kprint_hex64(next_rip);
        kprint("\n");
        rbp = next_rbp;
        rip = next_rip;
    }
}

__attribute__((noinline))
int isr_handle_error(uint64_t interrupt_number, uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t user_rsp) {
    if (interrupt_number == 14) {
        log_cr2();
    }

    kprint("  RIP: 0x");
    kprint_hex64(rip);
    kprint("\n");
    kprint("  CS: 0x");
    kprint_hex64(cs);
    kprint("\n");
    kprint("  RSP: 0x");
    kprint_hex64(user_rsp);
    kprint("\n");

    if (interrupt_number == 14) {
        log_error_code_breakdown(error_code);
    }

    {
        uint64_t cr2 = 0;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        log_memory_region(cr2);
    }

    {
        uint64_t rbp = 0;
        __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
        isr_print_stack_trace(rbp, rip);
    }

    trigger_blue_screen(interrupt_number, error_code);

    if (!current_thread_is_user()) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    isr_terminate_current_thread();
    return 0;
}

int isr_handle_interrupt(uint64_t interrupt_number, uint64_t rip, uint64_t cs) {
    trigger_blue_screen(interrupt_number, 0);
    if (!current_thread_is_user()) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    isr_terminate_current_thread();
    return 0;
}
