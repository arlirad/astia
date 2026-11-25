/* Shared library for the userspace linker test: imports fixture_base from
 * libfixture_base.so and exports fixture_answer to the payload. */
extern int fixture_base(void);
extern int fixture_base_tls(void);

/* Weak undefined: must resolve to zero instead of failing the link. */
extern int fixture_optional(void) __attribute__((weak));

int fixture_optional_value(void) {
    return fixture_optional != 0 ? fixture_optional() : 7;
}

int fixture_answer(void) {
    return fixture_base() + 2;
}

int fixture_tls_value(void) {
    return fixture_base_tls();
}
