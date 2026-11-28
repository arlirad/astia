#include <astia/api/syscalls.h>

#ifndef ASTIA_POWER_FIXTURE_OPERATION
#error \
    "ASTIA_POWER_FIXTURE_OPERATION must name ASTIA_POWER_SHUTDOWN or ASTIA_POWER_REBOOT"
#endif

void c_main(const long* frame) {
    (void) frame;

    const long result = astia_power(ASTIA_POWER_FIXTURE_OPERATION);

    astia_exit((int32_t) result);
}

__asm__(".globl _start\n"
        "_start:\n"
        "    movq %rsp, %rdi\n"
        "    call c_main\n"
        "    ud2\n");
