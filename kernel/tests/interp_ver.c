/*
 * Payload for the ld.astia.so symbol-version test.  The unversioned call
 * must bind the library's default (VERS_2) definition; the .symver call must
 * bind the non-default VERS_1 definition.
 */
#include <astia/api/syscalls.h>

extern int ver_value(void);
extern int ver_old(void);

__asm__(".symver ver_old,ver_value@VERS_1");

void _start(void) {
    if (ver_value() != 202)
        astia_exit(31);

    if (ver_old() != 101)
        astia_exit(32);

    if (ver_value() != 202)
        astia_exit(33);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
