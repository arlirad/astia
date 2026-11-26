/*
 * Read-only PT_LOAD target for a dynamic R_X86_64_RELATIVE relocation.  The
 * kernel applies the relocation when it maps the image; the interpreter must
 * leave it alone, because writing it again would fault.  .rorel has no write
 * flag, so the linker places it in a read-only LOAD segment.
 */

    .section .rorel,"a",@progbits
    .p2align 3
    .globl relro_pointer
    .type relro_pointer,@object
relro_pointer:
    .quad relro_target
    .size relro_pointer, 8

    .data
    .p2align 2
relro_target:
    .long 42
