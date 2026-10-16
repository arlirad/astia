.text
.global astia_context_switch_raw
.type astia_context_switch_raw, @function
astia_context_switch_raw:
    /* Save the callee-saved registers and a resume point. */
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    movq %rsp, 48(%rdi)
    leaq .Lresume(%rip), %rax
    movq %rax, 56(%rdi)

    /* Restore the next context and enter it. */
    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %r12
    movq 24(%rsi), %r13
    movq 32(%rsi), %r14
    movq 40(%rsi), %r15
    movq 48(%rsi), %rsp
    jmp *56(%rsi)

.Lresume:
    ret
.size astia_context_switch_raw, .-astia_context_switch_raw
