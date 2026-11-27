/* DSO providing a data object that the non-PIE payload copies into its own
 * image.  The address helper proves later DSO lookups resolve to the
 * executable's copy, not back to this definition. */
int copy_fixture_data = 0x2a;

int* copy_fixture_address(void) {
    return &copy_fixture_data;
}
