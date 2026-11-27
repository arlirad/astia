/* GNU IFUNC fixture.  The resolver calls into libfixture_base, so it can only
 * run once every image is relocated.  ifunc_internal references this DSO's
 * own IFUNC, which emits an R_X86_64_IRELATIVE relocation. */
extern int fixture_base_constant(void);

static int impl_a(void) {
    return 100;
}

static int impl_b(void) {
    return 200;
}

static void* pick(void) {
    return fixture_base_constant() == 7 ? (void*) impl_a : (void*) impl_b;
}

/* protected visibility keeps the DSO's own call binding locally, which makes
 * GNU ld emit R_X86_64_IRELATIVE for it. */
__attribute__((visibility("protected"))) int ifunc_value(void)
    __attribute__((ifunc("pick")));

int ifunc_internal(void) {
    return ifunc_value() + 1;
}
