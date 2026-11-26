/*
 * AArch64 cooperative context switch for the boot-CPU scheduler stage.
 *
 * The C++ side owns the context layout (src/arch/aarch64/context.hpp):
 *
 *   0x00 x19   0x08 x20   0x10 x21   0x18 x22
 *   0x20 x23   0x28 x24   0x30 x25   0x38 x26
 *   0x40 x27   0x48 x28   0x50 x29   0x58 x30 (LR)
 *   0x60 SP    0x68 DAIF
 *
 * Only x19-x28/FP/LR are callee-saved across an AAPCS64 call (Arm DDI 0487,
 * §5.1.1); SP is switched explicitly and DAIF is carried with the context so
 * interrupt masking survives a switch.  The scheduler masks before every
 * switch, so a restored DAIF never opens the gate behind the scheduler's back.
 */
.equ CONTEXT_X19,  0x00
.equ CONTEXT_X20,  0x08
.equ CONTEXT_X21,  0x10
.equ CONTEXT_X22,  0x18
.equ CONTEXT_X23,  0x20
.equ CONTEXT_X24,  0x28
.equ CONTEXT_X25,  0x30
.equ CONTEXT_X26,  0x38
.equ CONTEXT_X27,  0x40
.equ CONTEXT_X28,  0x48
.equ CONTEXT_FP,   0x50
.equ CONTEXT_LR,   0x58
.equ CONTEXT_SP,   0x60
.equ CONTEXT_DAIF, 0x68

.section .text
.balign 4
.global context_switch
.type context_switch, %function
context_switch:
    stp x19, x20, [x0, #CONTEXT_X19]
    stp x21, x22, [x0, #CONTEXT_X21]
    stp x23, x24, [x0, #CONTEXT_X23]
    stp x25, x26, [x0, #CONTEXT_X25]
    stp x27, x28, [x0, #CONTEXT_X27]
    str x29, [x0, #CONTEXT_FP]
    str x30, [x0, #CONTEXT_LR]
    mov x9, sp
    str x9, [x0, #CONTEXT_SP]
    mrs x9, daif
    str x9, [x0, #CONTEXT_DAIF]

    ldp x19, x20, [x1, #CONTEXT_X19]
    ldp x21, x22, [x1, #CONTEXT_X21]
    ldp x23, x24, [x1, #CONTEXT_X23]
    ldp x25, x26, [x1, #CONTEXT_X25]
    ldp x27, x28, [x1, #CONTEXT_X27]
    ldr x29, [x1, #CONTEXT_FP]
    ldr x9, [x1, #CONTEXT_SP]
    mov sp, x9
    ldr x9, [x1, #CONTEXT_DAIF]
    msr daif, x9
    ldr x30, [x1, #CONTEXT_LR]
    ret
.size context_switch, . - context_switch

/*
 * First entry of a fresh kernel context.  context_prepare_kernel_entry stores
 * the entry point in x19 and the argument in x20; the C++ start hook opens the
 * interrupt gate, publishes the scheduler record, and calls entry(argument).
 * Returning from an entry point retires the context.
 */
.balign 4
.global astia_aarch64_context_bootstrap
.type astia_aarch64_context_bootstrap, %function
astia_aarch64_context_bootstrap:
    mov x0, x19
    mov x1, x20
    bl astia_aarch64_context_start
    b astia_aarch64_context_returned
.size astia_aarch64_context_bootstrap, . - astia_aarch64_context_bootstrap
