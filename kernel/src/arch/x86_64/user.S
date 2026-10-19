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
   rax is fork's child-side zero; the remaining caller-saved
   registers are zeroed rather than left holding kernel values. */
astia_fork_iret:
    xorl %eax, %eax
    xorl %edi, %edi
    xorl %esi, %esi
    xorl %edx, %edx
    xorl %ecx, %ecx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    iretq
.size astia_fork_iret, .-astia_fork_iret
