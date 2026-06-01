[BITS 64]

global int80_stub
extern syscall_int80_dispatch
extern syscall_user_thread_exit

section .text

int80_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call syscall_int80_dispatch

    test rax, rax
    jnz .exit_thread

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

.exit_thread:
    mov rdi, rsp
    call syscall_user_thread_exit
.halt:
    hlt
    jmp .halt
