#include <astia/arch/x86_64/cpu_local.hpp>

.text
.global astia_syscall_entry
.type astia_syscall_entry, @function
astia_syscall_entry:
    /* SYSCALL leaves the user RSP in the architectural register. */
    /* GS points at the current CPU's anchor.  These values must not be
       process-wide: another CPU may enter a syscall while this one is
       constructing its frame. */
    movq %r12, %gs:ASTIA_GS_SYSCALL_SAVED_R12
    movq %rsp, %r12
    movq %gs:ASTIA_GS_SYSCALL_STACK_TOP, %rsp

    /* syscall_return: the return mode, the caller-saved image,
       user RIP, RFLAGS, RSP, then the callee-saved image (rbx, rbp,
       r12..r15) — pushed first, so it sits at the image's high end.  The
       syscall number and argument registers below are handed to the C
       door directly, not through this image. */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %gs:ASTIA_GS_SYSCALL_SAVED_R12 /* user R12 (r12 is the RSP stash) */
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

    /* Pass the call number, the six argument registers, and the return
       image as ordinary C parameters: rdi..r9 hold nr, a0..a4, and a5
       plus the image travel on the stack. */
    movq %rsp, %r10
    movq 0(%r10), %rdi            /* nr */
    movq 8(%r10), %rsi            /* a0 */
    movq 16(%r10), %rdx           /* a1 */
    movq 24(%r10), %rcx           /* a2 */
    movq 32(%r10), %r8            /* a3 */
    movq 40(%r10), %r9            /* a4 */
    pushq %r10                    /* eighth argument: return image */
    pushq 48(%r10)                /* seventh argument: a5 */
    /* SYSCALL masked IF through FMASK; the saved user RFLAGS is in the
       return image and iretq restores it, so handlers run with interrupts
       enabled — blocking syscalls must be able to
       halt-and-wake on timer ticks. */
    sti
    call astia_syscall_dispatch
    addq $16, %rsp                /* back to the return image base */
    /* A wakeup or timer may have requested this CPU while the syscall ran.
       Switch only at this syscall return boundary, never from IRQ dispatch. */
    pushq %rax
    call astia_reschedule_if_needed
    popq %rax

    /* Offset zero is syscall_return::mode, one of arch::return_mode's
       values. */
    cmpq $1, 0(%rsp)              /* arch::return_mode::signal */
    je .signal_return
    cmpq $2, 0(%rsp)              /* arch::return_mode::sigreturn */
    je .sigreturn_return
    jmp .normal_return

.signal_return:
    /* Signal entry: the interrupted register image holds the handler args. */
    movq 8(%rsp), %rdi
    movq 16(%rsp), %rsi
    movq 24(%rsp), %rdx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %eax, %eax
    jmp .build_iret_signal

.sigreturn_return:
    /* sigreturn's C result is the interrupted user return value; the
       remaining caller-saved registers were copied into the return image. */
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
