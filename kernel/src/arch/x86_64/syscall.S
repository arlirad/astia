.text
.global astia_syscall_entry
.type astia_syscall_entry, @function
astia_syscall_entry:
    /* SYSCALL leaves the user RSP in the architectural register. */
    /* GS points at the current CPU's anchor.  These values must not be
       process-wide: another CPU may enter a syscall while this one is
       constructing its frame. */
    movq %r12, %gs:16
    movq %rsp, %r12
    movq %gs:8, %rsp

    /* syscall_frame: nr, a0..a5, user RIP, RFLAGS, RSP, then the
       callee-saved image (rbx, rbp, r12..r15) — pushed first, so it
       sits at the frame's high end.  The exit path restores those six
       from here, and fork packs the child's resume from the whole
       thing. */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %gs:16                  /* user R12 (r12 is the RSP stash) */
    pushq %rbp
    pushq %rbx
    pushq %r12                           /* user RSP */
    pushq %r11
    pushq %rcx
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax

    movq %rsp, %rdi
    /* SYSCALL masked IF through FMASK; the saved user RFLAGS is on the
       frame and iretq restores it, so handlers run with interrupts
       enabled — blocking syscalls must be able to
       halt-and-wake on timer ticks. */
    sti
    call astia_syscall_dispatch
    /* A wakeup or timer may have requested this CPU while the syscall ran.
       Switch only at this syscall return boundary, never from IRQ dispatch. */
    pushq %rax
    call astia_reschedule_if_needed
    popq %rax

    cmpq $-1, 0(%rsp)
    je .signal_return
    cmpq $-2, 0(%rsp)
    je .sigreturn_return
    jmp .normal_return

.signal_return:
    /* Signal entry: frame.a0 is the signal number. */
    movq 8(%rsp), %rdi
    movq 16(%rsp), %rsi
    movq 24(%rsp), %rdx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %eax, %eax
    jmp .build_iret_signal

.sigreturn_return:
    /* sigreturn's C result is the interrupted user RAX; the remaining
       caller-saved registers were copied into the syscall frame. */
    movq %rax, %rcx
    movq 8(%rsp), %rdi
    movq 16(%rsp), %rsi
    movq 24(%rsp), %rdx
    movq 32(%rsp), %r10
    movq 40(%rsp), %r8
    movq 48(%rsp), %r9
    movq 96(%rsp), %rax
    jmp .build_iret_sigreturn

.normal_return:
    movq 96(%rsp), %rsi
    jmp .build_iret_normal

.build_iret_normal:
    movq 56(%rsp), %r12       /* user RIP */
    movq 64(%rsp), %r10       /* user RFLAGS */
    movq 72(%rsp), %r11       /* user RSP */
    movq 80(%rsp), %rbx
    movq 88(%rsp), %rbp
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    movq 96(%rsp), %rsi
    addq $128, %rsp
    pushq $0x1B
    pushq %r11
    pushq %r10
    pushq $0x23
    pushq %r12
    movq %rsi, %r12
    iretq

.build_iret_signal:
    movq 56(%rsp), %r12
    movq 64(%rsp), %r10
    movq 72(%rsp), %r11
    movq 80(%rsp), %rbx
    movq 88(%rsp), %rbp
    movq 96(%rsp), %rax
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    addq $128, %rsp
    pushq $0x1B
    pushq %r11
    pushq %r10
    pushq $0x23
    pushq %r12
    movq %rax, %r12
    iretq

.build_iret_sigreturn:
    movq 56(%rsp), %r12
    movq 64(%rsp), %r10
    movq 72(%rsp), %r11
    movq 80(%rsp), %rbx
    movq 88(%rsp), %rbp
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    addq $128, %rsp
    pushq $0x1B
    pushq %r11
    pushq %r10
    pushq $0x23
    pushq %r12
    movq %rax, %r12
    movq %rcx, %rax
    iretq
.size astia_syscall_entry, .-astia_syscall_entry
