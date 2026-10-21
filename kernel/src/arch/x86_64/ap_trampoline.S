.section .ap_trampoline, "ax"
.code16
.global astia_ap_trampoline_start
.global astia_ap_trampoline_end
.global astia_ap_cr3
.global astia_ap_stack
.global astia_ap_entry
.global astia_ap_cpu
astia_ap_trampoline_start:
    cli
    lgdt astia_ap_gdt_ptr
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $astia_ap_pm32

.code32
astia_ap_pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4
    movl astia_ap_cr3, %eax
    movl %eax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0
    ljmp $0x18, $astia_ap_long64

.code64
astia_ap_long64:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq astia_ap_stack, %rsp
    movq astia_ap_cpu, %rdi
    movq astia_ap_entry, %rax
    call *%rax
1:  hlt
    jmp 1b

.align 8
astia_ap_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF       /* 32-bit code */
    .quad 0x00CF92000000FFFF       /* 32-bit data */
    .quad 0x00AF9A000000FFFF       /* 64-bit code */
    .quad 0x00AF92000000FFFF       /* 64-bit data */
astia_ap_gdt_ptr:
    .word . - astia_ap_gdt - 1
    .long astia_ap_gdt

.align 8
astia_ap_cr3:  .long 0
astia_ap_stack: .quad 0
astia_ap_entry: .quad 0
astia_ap_cpu:   .quad 0
astia_ap_trampoline_end:
.size astia_ap_trampoline_start, .-astia_ap_trampoline_start
