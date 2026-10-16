.text
.global astia_load_gdt_tss
.type astia_load_gdt_tss, @function
astia_load_gdt_tss:
    /* rdi = GDTR pointer, si = TSS selector */
    lgdt (%rdi)
    pushq $0x08
    leaq .Lreload_cs(%rip), %rax
    pushq %rax
    lretq
.Lreload_cs:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movw %si, %ax
    ltr %ax
    ret
.size astia_load_gdt_tss, .-astia_load_gdt_tss
