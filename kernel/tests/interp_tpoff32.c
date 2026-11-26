/*
 * Payload for the TPOFF32 boot coverage.  Both reads go through a 32-bit TLS
 * offset: one names the defining library's own variable, the other names an
 * import that only libfixture_base.so defines.
 */
#include <astia/api/syscalls.h>

extern int fixture_tpoff32_local(void);
extern int fixture_tpoff32_imported(void);

void _start(void) {
    if (fixture_tpoff32_local() != 42)
        astia_exit(21);

    if (fixture_tpoff32_imported() != 1)
        astia_exit(22);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
