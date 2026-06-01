global switch_context


section .text
switch_context:
    ; Save flags + callee-saved registers of current context.
    ; Preserving IF is critical when switching out of syscall/IRQ paths.
    pushfq
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Store current stack pointer.
    mov [rdi], rsp

    ; Switch to next context stack.
    mov rsp, rsi

    ; Restore callee-saved registers + flags of next context.
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq
    ret
