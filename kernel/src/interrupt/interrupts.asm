[BITS 64]

global interrupts_enable
global interrupts_disable
global interrupts_are_enabled

section .text





interrupts_enable:
    sti
    ret





interrupts_disable:
    cli
    ret





interrupts_are_enabled:
    pushfq          
    pop rax         
    shr rax, 9      
    and rax, 1      
    ret
