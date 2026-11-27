/*
 * Payload for the ld.astia.so test.  fixture_answer is undefined here and
 * resolved by the interpreter from libfixture.so; the exit status proves the
 * GOT/PLT relocation, the transitive dependency, the dependency constructor
 * and the weak-undefined rule all landed.
 *
 * The first calls also exercise lazy PLT binding: ld_lazy_bindings is exported
 * by the interpreter and counts slots the resolver has bound.  GNU ld puts the
 * weak undefined hook in .plt.got with a GLOB_DAT (so c0 starts at zero); the
 * fixture calls are JUMP_SLOTs and bump the counter on first use.  The eager
 * variant (-DEXPECT_EAGER, linked -z now) proves those slots bind before entry
 * and never enter the resolver.
 */
#include <astia/api/syscalls.h>

#include <stdint.h>

extern int fixture_answer(void);
extern int fixture_variadic(int tag, ...);
extern int fixture_optional_value(void);
extern int fixture_tls_value(void);
extern int fixture_imported_tls(void);

extern uint64_t ld_lazy_bindings(void) __attribute__((weak));

/* The kernel enters _start without the SysV function-entry alignment, so a
 * bare C entry would call the lazy PLT with a misaligned stack. */
__attribute__((force_align_arg_pointer)) void _start(void) {
    const uint64_t c0 = ld_lazy_bindings();

    const int a1 = fixture_answer();

    const uint64_t c1 = ld_lazy_bindings();

    const int a2 = fixture_answer();

    const uint64_t c2 = ld_lazy_bindings();

#ifdef EXPECT_EAGER
    if (a1 != 42 || a2 != 42)
        astia_exit(25);

    if (c0 != 0 || c1 != 0 || c2 != 0)
        astia_exit(26);
#else
    if (ld_lazy_bindings == 0 || a1 != 42 || a2 != 42)
        astia_exit(25);

    if (c1 != c0 + 1)
        astia_exit(26);

    if (c2 != c1)
        astia_exit(27);
#endif

    if (fixture_variadic(1, 2, 2.5) != 28)
        astia_exit(28);

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
