/* Second-level library: imported by libfixture.so, which is imported by the
 * payload.  Its constructor proves DT_INIT_ARRAY runs before the payload, and
 * its thread-locals cover block offsets and cross-DSO TLS references. */
static int base_ready;

_Thread_local int fixture_tls        = 40;
_Thread_local int fixture_tls_second = 1;

__attribute__((constructor)) static void fixture_base_init(void) {
    base_ready = 40;
}

int fixture_base(void) {
    return base_ready;
}

int fixture_base_tls(void) {
    /* Both reads must land on their own slots: at least one is at a
       non-zero offset in the block. */
    return fixture_tls + fixture_tls_second - 1;
}

/* Lets an IFUNC resolver in another DSO call into a fully relocated image. */
int fixture_base_constant(void) {
    return 7;
}
