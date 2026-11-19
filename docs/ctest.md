# CTest suites

Configure and build the repository test tree from a clean directory:

```sh
cmake -S . -B build-tests -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
  -DASTIA_CROSS_USERSPACE=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

For the fastest complete local run, configure and build one tree, then run
CTest from that same tree:

```sh
time cmake --build build-tests --parallel
time ctest --test-dir build-tests --parallel "$(nproc)" --output-on-failure
```

The QEMU fixture preparation test builds the shared lifecycle and
module-only runtimes, both NVMe variants, and the virtio/USB/RTL8139 modules
once per tree. QEMU tests retain isolated initrds, disk images, and state
directories; their serial QEMU resource lock is unchanged. Test runners only
fall back to standalone configuration when invoked directly without
`ASTIA_TEST_BUILD`.

CTest can safely run independent host tests in parallel; QEMU and boot tests
remain serialized by the `astia-qemu` resource lock. Fast focused runs use
the existing labels without changing the authoritative full suite:

```sh
ctest --test-dir build-tests --parallel "$(nproc)" -L host --output-on-failure
ctest --test-dir build-tests --parallel "$(nproc)" -L 'nvme|virtio|usb' \
  --output-on-failure
```

The repository-level CMake project enables `ASTIA_BUILD_TESTS` by default, so
a fresh CLion CMake profile rooted at the repository registers the
deterministic tests for the CTest Application. Verify discovery without
running anything with:

```sh
ctest --test-dir build-tests --show-only=json-v1
```

External and manual suites remain registered for discovery, but are disabled
unless explicitly enabled. CLion can display a disabled CTest entry as failed
with `0 ms`; verify with `ctest -V -R`, which reports `Not Run (Disabled)` and
does not invoke the test command. This is not a missing executable failure.

The exact count is reported by `ctest --test-dir build-tests -N` because boot
tests are omitted when their host tools are unavailable. Set
`-DASTIA_BUILD_TESTS=OFF` only for a build that should expose the three
repository smoke tests without compiling the host test tree.

For the PR20 test tree, with `ASTIA_BUILD_TESTS=ON`,
`ASTIA_CROSS_USERSPACE=ON`, and both external and manual suites disabled,
CTest discovers 137 tests: 130 enabled and 7 intentionally disabled. The
seven disabled entries are `astia_zsh_pty_regression`,
`astia_zsh_signal_regression`, `astia_zsh_interactive_manual`,
`astia_nano_pty_smoke`, `astia_vga_graphical`, `astia_vga_pvh_fallback`, and
`astia_posix_batch`; they are registered for discovery but require external
or manual opt-in. The current `main` tree discovers 139 tests with the same
options, including 132 enabled tests. PR20 intentionally unregisters the
obsolete `astia_module_abi_layout`, `astia_module_c_abi_warning_flags`,
`astia_module_host_prepare`, `astia_qemu_prepare`, `astia_pipe_api_test`, and
`rtl8139_qemu_module_load` entries while adding the typed-contract,
I/O-request, NVMe-teardown, and virtio-blk-teardown tests. Therefore 130/130
is the correct enabled count for PR20, not a discovery or cache omission.

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
registered by default. External zsh, nano, and LTP tests remain disabled
unless `ASTIA_ENABLE_EXTERNAL_TESTS=ON` is set at configure time. Graphical
and interactive tests have `manual` labels and also require
`ASTIA_ENABLE_MANUAL_TESTS=ON`; they never run accidentally in CI. Keep both
options OFF in the normal CLion profile.

Enabling the external suites requires QEMU, `cpio`, and clean external source
trees. Provide a clean zsh checkout with `ASTIA_ZSH_SOURCE` and a clean LTP
checkout with `ASTIA_POSIX_TEST_SOURCE`; the nano smoke test additionally
needs a built nano binary supplied as `NANO_BINARY=/path/to/nano` when CTest
runs. VGA tests also require `grub-file`,
`grub-mkrescue`, `xorriso`, and Python. The graphical VGA test needs a usable
display, while the PVH fallback test uses the serial path. Enable the suites
explicitly when those prerequisites are available:

```sh
cmake -S . -B build-tests -DASTIA_BUILD_TESTS=ON \
  -DASTIA_ENABLE_EXTERNAL_TESTS=ON \
  -DASTIA_ENABLE_MANUAL_TESTS=ON \
  -DASTIA_ZSH_SOURCE=/path/to/zsh \
  -DASTIA_POSIX_TEST_SOURCE=/path/to/ltp
```

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
