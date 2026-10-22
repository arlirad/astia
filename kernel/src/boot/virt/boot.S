/*
 * boot.S - x86_64 boot entry.
 *
 * Use the x86/HVM direct boot ABI ("PVH"): QEMU picks the entry point out
 * of a .note.Xen ELF note, which lets `qemu-system-x86_64 -kernel ...`
 * load a 64-bit ELF directly (the multiboot loader in QEMU only accepts
 * i386 images and refuses x86-64 ones).
 *
 * QEMU enters _start in 32-bit protected mode. We install identity and
 * higher-half maps, switch to long mode and call kmain().
 *
 * Everything in this file lives in the .boot.* sections, which the linker
 * script keeps *identity-linked* (VMA == physical address): the 32-bit code
 * addresses tables, stack and GDT through truncated 32-bit pointers, long
 * before paging exists. The kernel proper links at image_base (see
 * cmake/linker/virt-x86_64.ld) and is only entered after paging is on.
 */

.set XEN_ELFNOTE_PHYS32_ENTRY, 18

/* ---- Multiboot2 image header --------------------------------------- */
/* The header is in the first load segment and is 8-byte aligned.  GRUB
 * selects _multiboot2_start through the entry-address tag. */
.section .multiboot2, "a"
.align 8
.global multiboot2_header
multiboot2_header:
    .long 0xE85250D6                 /* magic */
    .long 0                           /* i386 protected-mode architecture */
    .long 80                          /* header length */
    .long 0x17ADAEDA                 /* checksum */
    .word 1, 0                       /* request information */
    .long 28
    .long 1                           /* command line */
    .long 3                           /* modules */
    .long 6                           /* memory map */
    .long 14                          /* ACPI 1 RSDP */
    .long 15                          /* ACPI 2 RSDP */
    .long 0                           /* padding to 8-byte tag alignment */
    .word 6, 0                        /* module alignment */
    .long 8
    .word 3, 0                        /* entry address */
    .long 12
    .long _multiboot2_start
    .long 0                           /* padding to 8-byte end-tag alignment */
    .word 0, 0                        /* end tag */
    .long 8

/* ---- PVH entry note ------------------------------------------------ */
.section .note.Xen, "a", @note
.align 4
    .long 4                         /* n_namesz ("Xen\0") */
    .long 8                         /* n_descsz (64-bit ELF -> 8 bytes) */
    .long XEN_ELFNOTE_PHYS32_ENTRY  /* n_type */
    .asciz "Xen"
    .align 4
    .quad _start                    /* entry physical address */

/* ---- 32-bit entry -------------------------------------------------- */
.section .boot.text, "ax"
.code32
.global _start
.global _multiboot2_start
_start:
_multiboot2_start:
    cli
    movl $boot_stack_top, %esp
    movl %esp, %ebp

    /* Identity map the first 1 GiB using 2 MiB pages. */
    movl $boot_pd, %edi
    xorl %ecx, %ecx
    xorl %eax, %eax
1:
    movl %eax, %edx
    orl  $0x83, %edx                /* PRESENT | RW | PS(2MiB) */
    movl %edx, (%edi)
    movl $0, 4(%edi)
    addl $8, %edi
    addl $0x200000, %eax
    incl %ecx
    cmpl $512, %ecx
    jne  1b

    /* boot_pdpt[0] = boot_pd | PRESENT | RW */
    movl $boot_pd, %eax
    orl  $0x03, %eax
    movl %eax, boot_pdpt
    movl $0, boot_pdpt + 4

    /* boot_pml4[0] = boot_pdpt | PRESENT | RW */
    movl $boot_pdpt, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4
    movl $0, boot_pml4 + 4

    /* Higher-half windows over the same 1 GiB of physical memory:
     * PML4[256] reaches kernel_base and PML4[511] reaches image_base (where
     * the kernel image links). Each window's PDPT entry reuses boot_pd:
     * PD entries are relative to the 1 GiB window they are reached through,
     * so one table serves all three windows.
     *
     * kernel_base (0xFFFF800000000000) is exactly the base of its PML4 slot,
     * so its window sits at PDPT index 0. image_base (0xFFFFFFFF80000000)
     * sits at the *top* 2 GiB of its slot, i.e. PDPT index 510 - not 0. */
    movl $boot_pd, %eax
    orl  $0x03, %eax
    movl %eax, boot_pdpt_hi          /* kernel_base window: PDPT index 0 */
    movl $0, boot_pdpt_hi + 4
    movl %eax, boot_pdpt_img + 4080  /* image_base window: PDPT index 510 */
    movl $0, boot_pdpt_img + 4080 + 4

    movl $boot_pdpt_hi, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4 + 2048      /* 256 * 8: kernel_base */
    movl $0, boot_pml4 + 2048 + 4

    movl $boot_pdpt_img, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4 + 4088      /* 511 * 8: image_base */
    movl $0, boot_pml4 + 4088 + 4

    movl $boot_pml4, %eax
    movl %eax, %cr3

    /* Enable PAE (CR4.PAE). */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* Enable long mode (IA32_EFER.LME). */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $(1 << 8), %eax
    wrmsr

    /* Enable paging (CR0.PG) - PE is already set. */
    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0

    lgdt boot_gdt_ptr
    ljmp $0x08, $long_mode

/* ---- 64-bit entry -------------------------------------------------- */
.code64
long_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq $boot_stack_top, %rsp
    movq %rsp, %rbp

    /* QEMU's PVH option ROM (pc-bios/optionrom/pvh_main.c) jumps into the
     * kernel with %ebx = paddr of the hvm_start_info and %ecx = entry.
     * %ebx is untouched by the 32-bit setup above, so hand it to kmain as
     * its first argument. kmain links into the higher half, so a rel32 call
     * from this identity-mapped address would not reach: load it absolutely. */
    movl %ebx, %edi
    cld
    movabs $kmain, %rax
    call *%rax

1:  hlt
    jmp 1b

/* ---- descriptors --------------------------------------------------- */
.section .boot.rodata, "a"
.align 8
boot_gdt:
    .quad 0                         /* null */
    .quad 0x00AF9A000000FFFF        /* ring0, 64-bit code */
    .quad 0x00AF92000000FFFF        /* ring0 data */
boot_gdt_end:
boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt                  /* base (32-bit) for lgdt in protected mode */

/* ---- zero-initialised tables --------------------------------------- */
.section .boot.bss, "aw", @nobits
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096
boot_pdpt_hi:
    .skip 4096
boot_pdpt_img:
    .skip 4096
.align 16
boot_stack:
    .skip 16384
boot_stack_top:
