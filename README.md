<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Arlirad -->

# Astia

## Overview

**Astia** is a freestanding operating-system project for x86-64. It combines a
C++20 kernel, a native libc and ABI, loadable device modules, and small native
userspace programs. The primary runtime is a QEMU `virt` machine booted through
the x86/HVM (PVH) direct-boot ABI.

The project is under active development. The production target is
`ARCH=x86_64`, `PLATFORM=virt`, `BOOT=virt`; other architecture configurations
are not production targets.

## Key features

- **Modular kernel**: C++20, freestanding, no exceptions or RTTI, with memory
  management, processes, signals, TTYs, sockets, asynchronous I/O, and a VFS.
- **Native userspace**: Astia libc, `init`, `daemond`, and a growing set of
  coreutils are cross-built without the host C library.
- **Loadable drivers**: ET_REL modules use the public module ABI and generic
  block-device adapter.
- **Storage**: read-only ext4 integration, initrd/newc support, NVMe, and
  VirtIO block devices.
- **QEMU workflows**: interactive native zsh, optional GNU nano, boot tests,
  and hardware-driver fixtures.

## Project structure

The repository follows a layered kernel/userspace layout:

- `kernel/src/` and `kernel/include/`: kernel implementation and public ABI.
- `modules/`: loadable drivers and module ABI fixtures (`nvme`, `virtio-blk`).
- `libc/`: freestanding libc headers, implementation, and host tests.
- `init/`, `daemond/`, `coreutils/`: native init, service management, and
  userspace utilities.
- `toolchain/`: Clang sysroot, zsh/nano builds, initrd packaging, and QEMU
  launchers.
- `docs/`: ABI, filesystem, synchronization, and conformance notes.

## Build and run

Prerequisites include CMake 3.20+, a C/C++ toolchain, Clang for userspace,
QEMU, and the archive tools required by the test suite (`cpio`, `xxd`, `gzip`,
and `zstd`).

Build the supported kernel directly:

```sh
cmake -S kernel -B kernel/build \
  -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build kernel/build
```

Build the repository-level CMake project instead when working with an IDE or
the repository-level kernel project:

```sh
cmake -S . -B build -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build build
```

Boot the kernel in QEMU with the supported launcher:

```sh
./kernel/scripts/run-qemu.sh
```

The launcher uses `-kernel`, `-serial stdio`, and no display. Stop the idle
kernel with `Ctrl-C`.

## Userspace and QEMU workflows

The complete interactive zsh workflow builds the kernel test artifacts, Astia
libc/sysroot, coreutils, and an unpatched upstream zsh checkout before
launching QEMU:

```sh
./toolchain/run-zsh-qemu.sh
```

Use an existing clean zsh checkout and a reusable build directory when needed:

```sh
ZSH_SOURCE=/tmp/zsh ZSH_BUILD=/tmp/astia-zsh-build \
  ./toolchain/run-zsh-qemu.sh
```

The launcher is interactive; do not pipe or redirect its standard input. Set
`ASTIA_ZSH_HARNESS=1` for the automated PTY checks. `ASTIA_CLEAN_BUILD=1`
forces a clean kernel, zsh, and launcher build.

The GRUB/framebuffer-backed zsh workflow is available separately as
`toolchain/run-zsh-vga-qemu.sh`; it requires a real Multiboot2 framebuffer and
uses `/dev/ttv0` for shell I/O. See [`docs/zsh-vga.md`](docs/zsh-vga.md) for
the graphical smoke harness and explicit UART fallback.

GNU nano is opt-in. The launcher fetches and verifies the pinned source, builds
it in a private cache, and adds it to the initrd as `/bin/nano`:

```sh
ASTIA_NANO=1 ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

Run the repeated nano/zsh terminal regression with:

```sh
ASTIA_NANO=1 ASTIA_NANO_HARNESS=1 ASTIA_NANO_ITERATIONS=3 \
  ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

See [`toolchain/README.md`](toolchain/README.md) for sysroot construction,
non-interactive zsh regression, POSIX test selection, and build overrides.

## Storage modules

The module loader packages ET_REL objects into boot modules and invokes their
`module_main` entry point before PCI enumeration. Loaded modules remain
resident while registered devices reference their callbacks.

- **NVMe** (`modules/nvme`): discovers multiple namespaces, exposes stable
  paths such as `/dev/nvme0n1`, supports aligned asynchronous I/O, flush, MSI-X
  and polling fallback, and exercises controller reset recovery.
- **VirtIO block** (`modules/virtio-blk`): supports modern virtio-blk-pci,
  exposes `/dev/vblk0`, validates 512-byte geometry, and uses flush-backed
  writes. Devices without safe flush support are read-only.

Run the deterministic QEMU fixtures independently:

```sh
modules/nvme/run-qemu.sh
modules/virtio-blk/run-qemu.sh
VIRTIO_BLK_READONLY=1 modules/virtio-blk/run-qemu.sh
```

Module implementation and ABI details are documented in
[`modules/README.md`](modules/README.md),
[`modules/nvme/README.md`](modules/nvme/README.md), and
[`modules/virtio-blk/README.md`](modules/virtio-blk/README.md).

## Filesystems and initrds

The VFS currently includes initrd, devfs, tmpfs, and a bounded **read-only
ext4** implementation. ext4 supports the on-disk structures needed for regular
file and directory reads, including direct/indirect blocks and extent trees.
The ext4 fixture is generated separately with:

```sh
kernel/tests/make_ext4_fixture.sh
```

Production initrds are uncompressed `cpio` `newc` archives; `crc` remains
accepted for compatibility. gzip and zstd signatures are diagnosed but
compressed archives are not opened.

## Testing

Run the host-side kernel tests, module checks, fixture builds, and QEMU boot
tests through the kernel test launcher:

```sh
./kernel/scripts/test.sh kernel/build-tests
```

For a repository-level CTest build, use:

```sh
cmake -S . -B build-tests \
  -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
  -DASTIA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The Open POSIX harness uses an external LTP checkout and only claims the
selected categories listed in [`toolchain/README.md`](toolchain/README.md).
It is not a claim of complete POSIX conformance.

CTest suite names, labels, external-test opt-ins, and the reference test count
are documented in [`docs/ctest.md`](docs/ctest.md).

## Current limitations and roadmap

- x86-64 `virt` is the only supported production target. AArch64 is a
  compile-only bring-up probe, not a bootable kernel target.
- ext4 is read-only: journal replay, writes, fsck, xattrs, encryption, inline
  data, case folding, and complete HTree operations are unsupported.
- Compressed initrds are unsupported; only raw cpio archives can be passed to
  the kernel.
- POSIX timers, message queues, named/process-shared semaphores, `shm_open`,
  realtime queued-signal payloads, and `aio_fsync` remain unsupported. Some
  larger POSIX stress cases exceed current process/thread resource limits.
- The stackful `core::fiber` implementation is experimental and excluded from
  production builds.

Near-term work is tracked in [`docs/initrd-format.md`](docs/initrd-format.md),
[`docs/ext4-kmod-design.md`](docs/ext4-kmod-design.md), and
[`kernel/README.md`](kernel/README.md): compressed initrd decoding, safe
module-facing filesystem ownership and mount APIs, broader architecture and
platform policy providers, and tighter process/AIO/mapping ownership
boundaries.

## License and attributions

Astia project source and metadata are released under the MIT License.

- Copyright: Arlirad, 2026
- License: MIT (`SPDX-License-Identifier: MIT`)

The complete license text is in [`LICENSE`](LICENSE). Imported and generated
materials retain their own upstream license notices. The zsh, GNU nano, and
LTP workflows use external source trees; their upstream licenses and notices
remain applicable.
