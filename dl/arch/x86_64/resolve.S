/*
 * Lazy PLT entry for x86-64.
 *
 * A JUMP_SLOT PLT stub pushes its .rela.plt index and jumps to PLT0, which
 * pushes GOT[1] (the linker's opaque module pointer) and jumps to GOT[2].
 * On entry the stack is
 *
 *     0(%rsp)   module pointer
 *     8(%rsp)   .rela.plt index
 *    16(%rsp)   return address of the original caller
 *
 * The C resolver does all validation and symbol lookup.  Every caller-saved
 * argument register, %rax (whose %al carries the SysV variadic vector-register
 * count) and all xmm registers are preserved so the resolved function still
 * observes the call's arguments.  The two PLT words are dropped and the
 * resolved address is tail-jumped to in %r11, a caller-saved register the
 * callee may clobber, leaving the original return address and %al intact.
 * Program entry (_start) does not follow the usual function-entry stack
 * alignment, so the call stack is aligned explicitly rather than assumed.
 */
    .globl ld_lazy_resolve_entry
    .hidden ld_lazy_resolve_entry
    .type ld_lazy_resolve_entry, @function
ld_lazy_resolve_entry:
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    movq 72(%rsp), %rdi
    movq 80(%rsp), %rsi

    pushq %r12
    subq $256, %rsp
    movups %xmm0, 0(%rsp)
    movups %xmm1, 16(%rsp)
    movups %xmm2, 32(%rsp)
    movups %xmm3, 48(%rsp)
    movups %xmm4, 64(%rsp)
    movups %xmm5, 80(%rsp)
    movups %xmm6, 96(%rsp)
    movups %xmm7, 112(%rsp)
    movups %xmm8, 128(%rsp)
    movups %xmm9, 144(%rsp)
    movups %xmm10, 160(%rsp)
    movups %xmm11, 176(%rsp)
    movups %xmm12, 192(%rsp)
    movups %xmm13, 208(%rsp)
    movups %xmm14, 224(%rsp)
    movups %xmm15, 240(%rsp)

    movq %rsp, %r12
    andq $-16, %rsp
    call ld_lazy_resolve
    movq %rax, %r11
    movq %r12, %rsp

    movups 0(%rsp), %xmm0
    movups 16(%rsp), %xmm1
    movups 32(%rsp), %xmm2
    movups 48(%rsp), %xmm3
    movups 64(%rsp), %xmm4
    movups 80(%rsp), %xmm5
    movups 96(%rsp), %xmm6
    movups 112(%rsp), %xmm7
    movups 128(%rsp), %xmm8
    movups 144(%rsp), %xmm9
    movups 160(%rsp), %xmm10
    movups 176(%rsp), %xmm11
    movups 192(%rsp), %xmm12
    movups 208(%rsp), %xmm13
    movups 224(%rsp), %xmm14
    movups 240(%rsp), %xmm15
    addq $256, %rsp
    popq %r12

    addq $8, %rsp
    popq %r10
    popq %r9
    popq %r8
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax

    addq $16, %rsp
    jmp *%r11

    .size ld_lazy_resolve_entry, .-ld_lazy_resolve_entry

    .section .note.GNU-stack,"",@progbits
