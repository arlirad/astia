/*
 * AArch64 EL1 exception vector table and synchronous-exception probe.
 *
 * The table is the architectural 16-slot AArch64 layout: four exception
 * sources (synchronous, IRQ, FIQ, SError) in four entry groups (current EL
 * with SP_EL0, current EL with SP_ELx, lower EL using AArch64, lower EL using
 * AArch32).  Each slot is 128 bytes and the base is 2 KiB aligned, matching
 * VBAR_EL1[10:0] == RES0.
 *
 * The kernel runs at EL1h (SP_EL1), so a synchronous exception from the kernel
 * enters the current-EL-with-SPx slot at +0x200.  That slot saves a fixed
 * x0-x30 + ELR_EL1/SPSR_EL1/ESR_EL1/FAR_EL1 frame and calls the C++ fatal
 * reporter.  Every other slot uses the same frame and reporter with its vector
 * index, and is therefore a deterministic fatal stub.
 */

.equ FRAME_SIZE,      288
.equ FRAME_X0,        0x000
.equ FRAME_ELR,       0x0f8
.equ FRAME_SPSR,      0x100
.equ FRAME_ESR,       0x108
.equ FRAME_FAR,       0x110
.equ FRAME_RESERVED,  0x118

.macro save_exception_frame
    sub sp, sp, #FRAME_SIZE
    stp x0, x1, [sp, #0x000]
    stp x2, x3, [sp, #0x010]
    stp x4, x5, [sp, #0x020]
    stp x6, x7, [sp, #0x030]
    stp x8, x9, [sp, #0x040]
    stp x10, x11, [sp, #0x050]
    stp x12, x13, [sp, #0x060]
    stp x14, x15, [sp, #0x070]
    stp x16, x17, [sp, #0x080]
    stp x18, x19, [sp, #0x090]
    stp x20, x21, [sp, #0x0a0]
    stp x22, x23, [sp, #0x0b0]
    stp x24, x25, [sp, #0x0c0]
    stp x26, x27, [sp, #0x0d0]
    stp x28, x29, [sp, #0x0e0]
    str x30, [sp, #0x0f0]

    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #FRAME_ELR]

    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [sp, #FRAME_ESR]

    str xzr, [sp, #FRAME_RESERVED]
.endm

.macro vector_entry name, kind
    .global \name
    .type \name, %function
\name:
    save_exception_frame
    mov x0, sp
    mov x1, #\kind
    bl astia_aarch64_exception_entry
0:
    b 0b
    .size \name, . - \name
.endm

.section .vectors, "ax"
.balign 2048
.global __vector_table
.type __vector_table, %object
__vector_table:
    vector_entry __vector_current_el_sp0_sync, 0
    .org __vector_table + 0x080
    vector_entry __vector_current_el_sp0_irq, 1
    .org __vector_table + 0x100
    vector_entry __vector_current_el_sp0_fiq, 2
    .org __vector_table + 0x180
    vector_entry __vector_current_el_sp0_serror, 3
    .org __vector_table + 0x200
    vector_entry __vector_current_el_spx_sync, 4
    .org __vector_table + 0x280
    vector_entry __vector_current_el_spx_irq, 5
    .org __vector_table + 0x300
    vector_entry __vector_current_el_spx_fiq, 6
    .org __vector_table + 0x380
    vector_entry __vector_current_el_spx_serror, 7
    .org __vector_table + 0x400
    vector_entry __vector_lower_el_aarch64_sync, 8
    .org __vector_table + 0x480
    vector_entry __vector_lower_el_aarch64_irq, 9
    .org __vector_table + 0x500
    vector_entry __vector_lower_el_aarch64_fiq, 10
    .org __vector_table + 0x580
    vector_entry __vector_lower_el_aarch64_serror, 11
    .org __vector_table + 0x600
    vector_entry __vector_lower_el_aarch32_sync, 12
    .org __vector_table + 0x680
    vector_entry __vector_lower_el_aarch32_irq, 13
    .org __vector_table + 0x700
    vector_entry __vector_lower_el_aarch32_fiq, 14
    .org __vector_table + 0x780
    vector_entry __vector_lower_el_aarch32_serror, 15
    .org __vector_table + 0x800
.global __vector_table_end
__vector_table_end:
.size __vector_table, __vector_table_end - __vector_table

.section .text
.balign 4
.global astia_aarch64_brk_probe
.type astia_aarch64_brk_probe, %function
astia_aarch64_brk_probe:
    brk #0x1234
0:
    b 0b
.size astia_aarch64_brk_probe, . - astia_aarch64_brk_probe
