/*
 * Payload for the ld.astia.so test.  fixture_answer is undefined here and
 * resolved by the interpreter from libfixture.so; the exit status proves the
 * GOT/PLT relocation, the transitive dependency, the dependency constructor
 * and the weak-undefined rule all landed.
 */
#include <astia/api/syscalls.h>

extern int fixture_answer(void);
extern int fixture_optional_value(void);
extern int fixture_tls_value(void);
extern int fixture_imported_tls(void);

void _start(void) {
    if (fixture_answer() != 42)
        astia_exit(21);

    if (fixture_optional_value() != 7)
        astia_exit(22);

    if (fixture_tls_value() != 40)
        astia_exit(23);

    if (fixture_imported_tls() != 1)
        astia_exit(24);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
