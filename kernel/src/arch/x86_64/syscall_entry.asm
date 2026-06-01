global syscall_entry
extern syscall_dispatch

section .text
syscall_entry:
    swapgs

    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rcx
    push r11

    mov rdi, rsp
    call syscall_dispatch
    mov [rsp + 8*8], rax

    pop r11
    pop rcx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax

    swapgs
    sysretq
