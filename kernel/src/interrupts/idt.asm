; idt.asm
global idt_load

idt_load:
    lidt [rdi]      ; rdi = Pointer to idt_ptr
    ret