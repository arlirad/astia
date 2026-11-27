/* GNU-hash-only library: no DT_HASH, so the loader must take the dynamic
 * symbol count from .gnu.hash.  The second export is never referenced by the
 * payload, so a count that stops at the first resolved symbol is visible as a
 * failed relocation rather than a working call. */
int fixture_gnu_unused(void) {
    return -1;
}

int fixture_gnu_answer(void) {
    return 42;
}
