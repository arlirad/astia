# CTest suites

Configure and build the repository test tree from a clean directory:

```sh
cmake -S . -B build-tests -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
  -DASTIA_BUILD_TESTS=ON -DASTIA_CROSS_USERSPACE=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The reference toolchain registers 124 tests. The exact count is reported by
`ctest --test-dir build-tests -N` because boot tests are omitted when their
host tools are unavailable.

Use labels to select suites:

```sh
ctest --test-dir build-tests -N
ctest --test-dir build-tests -L 'kernel|boot'
ctest --test-dir build-tests -L libc
ctest --test-dir build-tests -L 'nvme|virtio|usb'
ctest --test-dir build-tests -L ext4
ctest --test-dir build-tests -L qemu --output-on-failure
```

The deterministic host, fixture, module, ext4, boot, and local QEMU tests are
registered by default when `ASTIA_BUILD_TESTS=ON`. External zsh, nano, and LTP
tests are visible but disabled unless `ASTIA_ENABLE_EXTERNAL_TESTS=ON` is set
at configure time. Graphical and interactive tests have `manual` labels and
also require `ASTIA_ENABLE_MANUAL_TESTS=ON`; they never run accidentally in CI.

External source trees can be supplied without changing the repository:

```sh
cmake -S . -B build-tests -DASTIA_BUILD_TESTS=ON \
  -DASTIA_ENABLE_EXTERNAL_TESTS=ON \
  -DASTIA_ZSH_SOURCE=/path/to/zsh \
  -DASTIA_POSIX_TEST_SOURCE=/path/to/ltp
ctest --test-dir build-tests -L external --output-on-failure
```

CTest test names identify focused hardware cases, including
`astia_nvme_msix`, `astia_nvme_polling`, `astia_virtio_blk_read_write`,
`astia_virtio_blk_read_only`, `astia_vga_graphical`, and
`astia_vga_pvh_fallback`. All QEMU tests use a resource lock and bounded
timeouts so parallel CTest runs do not race or hang.
