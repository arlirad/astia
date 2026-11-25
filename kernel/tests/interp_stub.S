/*
 * Minimal PT_INTERP target for the exec bootstrap test.
 *
 * Proves it ran by stamping a magic argc, then walks argv/envp to auxv and
 * jumps to the payload's AT_ENTRY.  Self-contained: no relocations, no TLS,
 * no dependencies, so the kernel's shared-image loader accepts it.
 */
    .globl _start
_start:
    movq $0x1234, (%rsp)

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
find_entry:
    movq (%rbx), %rax
    testq %rax, %rax
    jz fail
    cmpq $9, %rax            /* AT_ENTRY */
    je jump
    addq $16, %rbx
    jmp find_entry
jump:
    movq 8(%rbx), %rax
    jmp *%rax

fail:
    movq $8, %rax            /* SYS_EXIT */
    movq $1, %rdi
    syscall
