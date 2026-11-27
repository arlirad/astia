/* Shared library for the userspace linker test: imports fixture_base from
 * libfixture_base.so and exports fixture_answer to the payload. */
extern int fixture_base(void);
extern int fixture_base_tls(void);

/* Cross-DSO TLS: this TPOFF64 names an undefined symbol and must resolve
 * against the *defining* module's block offset. */
extern _Thread_local int fixture_tls_second;

/* Weak undefined: must resolve to zero instead of failing the link. */
extern int fixture_optional(void) __attribute__((weak));

int fixture_optional_value(void) {
    return fixture_optional != 0 ? fixture_optional() : 7;
}

int fixture_answer(void) {
    return fixture_base() + 2;
}

/* Variadic arguments exercise %al on the lazy PLT path: the callee only saves
 * its xmm argument registers when %al reports the vector-register count. */
int fixture_variadic(int tag, ...) {
    __builtin_va_list args;

    __builtin_va_start(args, tag);

    const int    count = __builtin_va_arg(args, int);
    const double scale = __builtin_va_arg(args, double);

    __builtin_va_end(args);

    return tag + count + (int) (scale * 10.0);
}

int fixture_tls_value(void) {
    return fixture_base_tls();
}

int fixture_imported_tls(void) {
    return fixture_tls_second;
}
