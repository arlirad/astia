.section .rodata
.balign 8
.global __kernel_symbol_table_start
__kernel_symbol_table_start:
.long 0x41535453
.short 1
.short 16
.long 1
.long 36
.long 63
.quad 0x123456789abcdef0
.long 0
.long 0
.asciz "_ZN14kernel_fixture4biasEv"
.global __kernel_symbol_table_end
__kernel_symbol_table_end:
