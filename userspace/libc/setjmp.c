#include <setjmp.h>

#if !defined(__x86_64__)
#error "setjmp/longjmp only implemented for x86_64"
#endif

__attribute__((naked, returns_twice)) int setjmp(jmp_buf env) {
    __asm__ volatile(
        "movq %rbx, 0(%rdi)\n\t"
        "movq %rbp, 8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"
        "leaq 8(%rsp), %rcx\n\t"
        "movq %rcx, 48(%rdi)\n\t"
        "movq %rax, 56(%rdi)\n\t"
        "xor %eax, %eax\n\t"
        "ret\n\t"
    );
}

__attribute__((naked, noreturn)) void longjmp(jmp_buf env, int val) {
    __asm__ volatile(
        "movl %esi, %eax\n\t"
        "test %eax, %eax\n\t"
        "jne 1f\n\t"
        "movl $1, %eax\n\t"
        "1:\n\t"
        "movq 0(%rdi), %rbx\n\t"
        "movq 8(%rdi), %rbp\n\t"
        "movq 16(%rdi), %r12\n\t"
        "movq 24(%rdi), %r13\n\t"
        "movq 32(%rdi), %r14\n\t"
        "movq 40(%rdi), %r15\n\t"
        "movq 48(%rdi), %rsp\n\t"
        "jmp *56(%rdi)\n\t"
    );
}
