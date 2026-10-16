.text
.global astia_syscall_entry
.type astia_syscall_entry, @function
astia_syscall_entry:
    /* SYSCALL leaves the user RSP in the architectural register. */
    movq %r12, astia_syscall_saved_r12(%rip)
    movq %rsp, %r12
    movq astia_syscall_stack_top(%rip), %rsp

    /* syscall_frame: nr, a0..a5, user RIP, RFLAGS, RSP, then the
       callee-saved image (rbx, rbp, r12..r15) — pushed first, so it
       sits at the frame's high end.  The exit path restores those six
       from here, and fork packs the child's resume from the whole
       thing. */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq astia_syscall_saved_r12(%rip) /* user R12 (r12 is the RSP stash) */
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
       enabled — blocking syscalls (io_enter waits) must be able to
       halt-and-wake on timer ticks. */
    sti
    call astia_syscall_dispatch

    /* Build an iretq frame from the saved user return state. */
    movq 56(%rsp), %r12       /* user RIP */
    movq 64(%rsp), %r10       /* user RFLAGS */
    movq 72(%rsp), %r11       /* user RSP */
    movq 80(%rsp), %rbx
    movq 88(%rsp), %rbp
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    addq $128, %rsp
    pushq $0x1B                /* user SS */
    pushq %r11
    pushq %r10
    pushq $0x23                /* user CS */
    pushq %r12
    /* User R12 returns from the frame, not the global stash: two
       threads' syscalls must not race for one scratch slot (the
       stash above survives only because this entry must free %r12
       before it can push anything).  After the five iretq pushes the
       frame base sits 88 below, so slot 96 is 8(%rsp). */
    movq 8(%rsp), %r12
    iretq
.size astia_syscall_entry, .-astia_syscall_entry
