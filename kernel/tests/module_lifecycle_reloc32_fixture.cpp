extern "C" {
int reloc32_target = 0x12345678;
}

asm(".section .data.reloc32,\"aw\",@progbits\n"
    ".p2align 3\n"
    ".globl reloc32_slot\n"
    "reloc32_slot:\n"
    ".long reloc32_target\n"
    ".previous\n");

int module_main() {
    return 0;
}
