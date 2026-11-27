/* Library with both DT_HASH and DT_GNU_HASH.  The loader must keep using the
 * SysV chain count for such an image instead of switching to the GNU hash
 * heuristic. */
int fixture_semihash_answer(void) {
    return 21;
}
