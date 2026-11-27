/* RTLD_NODELETE fixture: stays mapped after its last handle closes. */
int dl_nd_value(void) {
    return 9;
}
