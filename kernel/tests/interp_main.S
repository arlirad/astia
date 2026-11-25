/*
 * Payload for the PT_INTERP bootstrap test: exits 0 only if the interpreter
 * stamped argc and AT_BASE is set, i.e. the kernel both loaded and entered
 * the interpreter.
 */
    .globl _start
_start:
    cmpq $0x1234, (%rsp)
    jne fail

    lea 8(%rsp), %rbx
skip_argv:
    cmpq $0, (%rbx)
    je env_start
    addq $8, %rbx
    jmp skip_argv
env_start:
    addq $8, %rbx
skip_env:
    cmpq $0, (%rbx)
    je auxv
    addq $8, %rbx
    jmp skip_env

auxv:
    addq $8, %rbx
find_base:
    movq (%rbx), %rax
    testq %rax, %rax
    jz fail
    cmpq $7, %rax            /* AT_BASE */
    je check
    addq $16, %rbx
    jmp find_base
check:
    cmpq $0, 8(%rbx)
    je fail
    xorq %rdi, %rdi
    movq $8, %rax            /* SYS_EXIT */
    syscall

fail:
    movq $1, %rdi
    movq $8, %rax
    syscall
