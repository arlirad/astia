/*
 * QEMU virt direct ELF entry for the production AArch64 early boot stage.
 *
 * QEMU treats an ELF -kernel image as bare metal: it jumps to the ELF entry
 * without a firmware reset, so no stack is supplied and x0 is not the DTB.
 * The DTB is located at the start of RAM by the C++ stage.  This entry only
 * clears .bss, establishes the stage stack, preserves x0 for diagnostics and
 * calls the C++ production entry.
 */

.section .text.boot, "ax"
.align 4
.global _start
.type _start, %function
_start:
    mov x19, x0
    adrp x1, __bss_start
    add x1, x1, :lo12:__bss_start
    adrp x2, __bss_end
    add x2, x2, :lo12:__bss_end
1:
    cmp x1, x2
    b.hs 2f
    str xzr, [x1], #8
    b 1b
2:
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1
    mov x0, x19
    bl astia_aarch64_early_main
3:
    wfe
    b 3b
.size _start, . - _start
