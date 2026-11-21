/*
 * Minimal production PID-1 fixture: the selected init runs once as PID 1
 * under the kernel process, does a bounded amount of real work, and exits 0.
 * The boot harness checks that the kernel reports the launch, waits for the
 * exit, and only then treats the exit as fatal.
 */
#include <astia/api/syscalls.h>

void c_main(const long* frame) {
    (void) frame;
    if (astia_getpid() != 1 || astia_getppid() != 0)
        astia_exit(1);
    for (int i = 0; i < 256; ++i) {
        astia_timespec now;
        if (astia_clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_nsec < 0 ||
            now.tv_nsec >= 1000000000LL)
            astia_exit(1);
    }
    astia_exit(0);
}

__asm__(".globl _start\n"
        "_start:\n"
        "    movq %rsp, %rdi\n"
        "    call c_main\n"
        "    ud2\n");
