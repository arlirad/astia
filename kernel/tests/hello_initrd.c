// The module that rides the boot initrd (see tests/CMakeLists.txt): a bare
// ET_REL object with no imports, so loading it needs nothing exported by the
// kernel — the boot test can prove the whole path (PVH modlist -> tar walk ->
// ELF load -> run) on its own. `module_main` bumps a static counter
// (40 -> 42) and returns it; the test greps for `rc=42` in the serial log.

static int counter = 40;

int module_main(void) {
    counter += 2;
    return counter;
}
