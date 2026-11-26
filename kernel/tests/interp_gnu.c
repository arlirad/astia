/*
 * Payload for the GNU-hash boot coverage.  It is built with --hash-style=gnu,
 * so its own symbol table has no DT_HASH either: the interpreter has to count
 * the undefined import from the .dynsym/.dynstr adjacency.  Its dependencies
 * are one GNU-hash-only library and one library carrying both tables.
 */
#include <astia/api/syscalls.h>

extern int fixture_gnu_answer(void);
extern int fixture_semihash_answer(void);

void _start(void) {
    if (fixture_gnu_answer() != 42)
        astia_exit(21);

    if (fixture_semihash_answer() != 21)
        astia_exit(22);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
