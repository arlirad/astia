/* Second-level library: imported by libfixture.so, which is imported by the
 * payload.  Its constructor proves DT_INIT_ARRAY runs before the payload, and
 * its _Thread_local proves the interpreter laid out and populated a static
 * TLS block for a shared object. */
static int base_ready;

_Thread_local int fixture_tls = 40;

__attribute__((constructor)) static void fixture_base_init(void) {
    base_ready = 40;
}

int fixture_base(void) {
    return base_ready;
}

int fixture_base_tls(void) {
    return fixture_tls;
}
