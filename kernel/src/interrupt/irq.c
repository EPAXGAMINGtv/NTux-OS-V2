#include <interrupt/irq.h>
#include <interrupt/pic.h>
#include <interrupt/apic/apic.h>
#include <drivers/framebuffer/kprint.h>

extern uint64_t irq_stub_table[];

void (*irq_handlers[16])(void) = {0};  

void irq_register_handler(int irq, void (*handler)(void)) {
    if (irq < 16) {
        irq_handlers[irq] = handler;  
    }
}

void irq_handler_c(uint64_t *stack) {
    if (!stack) {
        return;
    }
    int irq = (int)stack[15];

    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    if (apic_uses_ioapic()) {
        apic_send_eoi();
    }
    pic_send_eoi(irq);
}
