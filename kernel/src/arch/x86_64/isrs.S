/* Interrupt entry common path (x86_64).
 *
 * Every stub in isrs_gen.S has pushed a uniform frame — [dummy] error code,
 * then vector number — on top of the CPU-pushed frame (RIP, CS, RFLAGS; a
 * ring transition would add RSP/SS, which we do not have yet, so the struct
 * in interrupts.cpp ends at rflags). isr_common saves the GPRs, 16-byte
 * aligns the stack (interrupts may land in a leaf function using the red
 * zone, and the pushed frame has an arbitrary size modulo 16), and calls
 * interrupt_dispatch(registers*). rbx carries the frame pointer across the
 * call because it is callee-saved; the value actually observed by the
 * interrupted code is restored from the stack.
 *
 * The register push order below defines the `registers` field order in
 * interrupts.cpp (lowest address = last pushed = first field).
 */

.text
.global isr_common
isr_common:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx         /* frame pointer; callee-saved, survives the call */
    andq $-16, %rsp         /* SysV 16-byte alignment for the call */
    movq %rbx, %rdi         /* arg1: the register frame */
    call interrupt_dispatch
    movq %rbx, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    addq $16, %rsp          /* drop vector + error code */
    iretq
