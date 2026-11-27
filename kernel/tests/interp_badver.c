/*
 * Payload for the ld.astia.so version rejection test.  It requires
 * bad_value@VERS_1; the runtime /lib/libverstub.so only defines VERS_2, so
 * the loader must fail instead of binding the unversioned VERS_2 definition.
 * If the call succeeds, exit 0 so the boot test notices the wrong bind.
 */
#include <astia/api/syscalls.h>

extern int bad_old(void);

__asm__(".symver bad_old,bad_value@VERS_1");

void _start(void) {
    (void) bad_old();

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
