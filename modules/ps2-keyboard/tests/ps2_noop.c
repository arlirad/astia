#include <astia/api/syscalls.h>

__attribute__((used)) static void start(void) {
    astia_exit(0);

    for (;;) {
        ;
    }
}

__asm__(".globl _start\n"
        "_start:\n"
        "    andq $-16, %rsp\n"
        "    call start\n"
        "    ud2\n");
