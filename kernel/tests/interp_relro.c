#include <astia/api/syscalls.h>

volatile int relro_probe __attribute__((section(".data.rel.ro"))) = 5;
volatile int plain_probe                                          = 1;

__attribute__((force_align_arg_pointer, noreturn)) void _start(void) {
    plain_probe = 2;
    if (plain_probe != 2)
        astia_exit(81); /* non-RELRO data must stay writable */
#ifdef RELRO_CONTROL
    astia_exit(0); /* control build: the plain-data write is the whole test */
#else
    relro_probe = 6; /* must fault once RELRO is applied */

    astia_exit(0); /* reached only if the write succeeded: failure */
#endif
    for (;;) {
        __asm__ volatile("hlt");
    }
}
