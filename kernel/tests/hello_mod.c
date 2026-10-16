// A minimal loadable kernel module, compiled on the host into an ET_REL object
// and embedded into the test binary (see tests/CMakeLists.txt). It exercises
// both relocation classes the loader must handle:
//   - a PC32 reference to its own .data (the `counter` global), and
//   - a PLT32 call to an imported symbol (`kernel_add`), which the test
//     resolves to a host function through the export table.
// `module_main` bumps counter (40 -> 42) and returns kernel_add(42, 0) = 42.

extern int kernel_add(int a, int b);

static int counter = 40;

int module_main(void) {
    counter += 2;
    return kernel_add(counter, 0);
}
