/*
 * Payload for the read-only RELATIVE coverage.  relro_pointer lives in a
 * read-only LOAD segment and holds an address that only exists after the
 * kernel applies the image's R_X86_64_RELATIVE relocation.  If the
 * interpreter re-applied or skipped it, the dereference either faults or
 * reads the link-time address.
 */
#include <astia/api/syscalls.h>

extern const int* const relro_pointer;

void _start(void) {
    if (*relro_pointer != 42)
        astia_exit(21);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
