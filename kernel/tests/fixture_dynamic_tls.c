/* Global-dynamic TLS fixture: the default model emits DTPMOD64/DTPOFF64
 * GOT slots and calls __tls_get_addr, which ld.astia.so exports. */
_Thread_local int dyn_tls = 77;

int dyn_tls_value(void) {
    return dyn_tls;
}

void dyn_tls_set(int value) {
    dyn_tls = value;
}
