.text
.global context_user_iret
.type context_user_iret, @function
context_user_iret:
    movq %r12, %rdi
    movq %r13, %rsi
    iretq
.size context_user_iret, .-context_user_iret

.global astia_fork_iret
.type astia_fork_iret, @function
/* Resume a forked child in ring 3.  astia_context_switch_raw restored
   the callee-saved registers from the context (the parent's image,
   prepared by context_prepare_fork) and left RSP on the synthetic
   iretq frame: parent RIP, ring-3 CS/SS, parent RFLAGS, parent RSP.
   rax is fork's child-side zero.  The other syscall-preserved argument
   registers sit immediately below the iret frame. */
astia_fork_iret:
    xorl %eax, %eax
    movq -48(%rsp), %rdi
    movq -40(%rsp), %rsi
    movq -32(%rsp), %rdx
    movq -24(%rsp), %r10
    movq -16(%rsp), %r8
    movq -8(%rsp), %r9
    xorl %r11d, %r11d
    iretq
.size astia_fork_iret, .-astia_fork_iret
