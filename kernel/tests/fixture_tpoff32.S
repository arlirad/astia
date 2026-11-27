/*
 * TLS fixture for the R_X86_64_TPOFF32 loader path.  Linkers refuse to emit a
 * dynamic TPOFF32 in a shared object, so this file writes both accesses as
 * GOT-based initial-exec and reloc_type_rewrite.py retypes the resulting
 * TPOFF64 GOT slots to TPOFF32 afterwards.  The accessor loads the slot, takes
 * its low 32 bits and sign-extends them, which is how a TPOFF32 relocation is
 * applied: the loader writes only the low four bytes.
 *
 * fixture_tls32_local is defined here (the same-module branch); the imported
 * accessor names fixture_tls_second, owned by libfixture_base.so (the
 * defining-module branch).
 */

    .section .tdata,"awT",@progbits
    .p2align 2
    .globl fixture_tls32_local
    .type fixture_tls32_local,@tls_object
fixture_tls32_local:
    .long 42
    .size fixture_tls32_local, 4

    .text
    .globl fixture_tpoff32_local
    .type fixture_tpoff32_local,@function
fixture_tpoff32_local:
    movq fixture_tls32_local@gottpoff(%rip), %rax
    movslq %eax, %rax
    movl %fs:(%rax), %eax
    ret
    .size fixture_tpoff32_local,.-fixture_tpoff32_local

    .type fixture_tls_second,@tls_object
    .globl fixture_tpoff32_imported
    .type fixture_tpoff32_imported,@function
fixture_tpoff32_imported:
    movq fixture_tls_second@gottpoff(%rip), %rax
    movslq %eax, %rax
    movl %fs:(%rax), %eax
    ret
    .size fixture_tpoff32_imported,.-fixture_tpoff32_imported
