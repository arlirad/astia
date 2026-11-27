/*
 * Payload for IFUNC resolution.  ifunc_value is an STT_GNU_IFUNC symbol in
 * libfixture_ifunc.so; its resolver calls into libfixture_base and picks the
 * implementation.  A second call proves the slot holds the result instead of
 * the resolver, and ifunc_internal covers the DSO's own IRELATIVE slot.
 */
#include <astia/api/syscalls.h>

extern int ifunc_value(void);
extern int ifunc_internal(void);

void _start(void) {
    if (ifunc_value() != 100)
        astia_exit(41);

    if (ifunc_value() != 100)
        astia_exit(42);

    if (ifunc_internal() != 101)
        astia_exit(43);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
