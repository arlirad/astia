/*
 * Interpreter entry.  %rsp still holds the kernel-built startup frame; keep
 * it, run the linker on a 16-byte aligned stack, then restore it before
 * entering the payload so argc/argv/auxv are where its _start expects them.
 */
    .globl ld_start
    .hidden ld_main
ld_start:
    movq %rsp, ld_saved_stack(%rip)
    andq $-16, %rsp
    movq ld_saved_stack(%rip), %rdi
    call ld_main
    movq ld_saved_stack(%rip), %rsp
    jmp *%rax

    .section .bss
    .align 8
ld_saved_stack:
    .quad 0
