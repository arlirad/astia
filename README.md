<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Arlirad -->

<p align="center">
  <img src="img/banner.png" alt="Astia banner" width="320px">
</p>

> A small, freestanding operating-system project for learning, experimentation,
> and systems programming on QEMU.

Astia is an x86_64 hobby OS with a C++20 kernel, asynchronous I/O, and a
QEMU-first development workflow.

- [Build the kernel](#build-and-run)
- [Run the interactive userspace](#userspace-and-qemu-workflows)
- [Read the documentation](#documentation)
- [Contribute](https://github.com/arlirad/astia/issues)
- [MIT license](LICENSE)

## What Astia is

Astia is a freestanding operating-system project for x86_64. It combines a
C++20 kernel, a native libc and ABI, loadable device modules, and small native
userspace programs. The supported boot target is a QEMU `virt` machine loaded
through the x86/HVM (PVH) direct-boot ABI.

The project is under active development and is not a production-ready general
purpose operating system. The only supported boot configuration is
`ARCH=x86_64`, `PLATFORM=virt`, `BOOT=virt`.

## What Astia is not

Astia is not a complete POSIX implementation, a full TCP/IP stack, or a
portable hardware platform. Several subsystems are intentionally bounded,
experimental, or still being integrated; see
[Current limitations and roadmap](#current-limitations-and-roadmap).

## Key features

- **Typed kernel core**: C++20, freestanding, no exceptions or RTTI, with
  memory management, processes, signals, TTYs, UNIX sockets, and asynchronous
  I/O.
- **VFS and filesystems**: initrd, devfs, tmpfs, and bounded read-only ext4
  support, with regular-file and directory reads.
- **Native userspace**: Astia libc, `init`, `daemond`, and a growing set of
  coreutils are cross-built without the host C library.
- **Typed loadable modules**: ET_REL modules use the public C++ registration
  contracts and generic device adapters.
- **Device and network foundations**: NVMe and VirtIO block modules, plus
  bounded RTL8139 and USB/xHCI work for QEMU fixtures.
- **Storage**: read-only ext4 integration and initrd/newc support.
- **QEMU workflows**: interactive native zsh, optional GNU nano, boot tests,
  and hardware-driver fixtures.

## Project structure

The repository follows a layered kernel/userspace layout:

- `kernel/src/` and `kernel/include/`: kernel implementation and public ABI.
- `modules/`: loadable drivers and typed module fixtures (`nvme`, `virtio-blk`).
- `libc/`: freestanding libc headers, implementation, and host tests.
- `init/`, `daemond/`, `coreutils/`: native init, service management, and
  userspace utilities.
- `pkg/`: sandboxed host-Linux package manager prototype and static repository
  format.
- `toolchain/`: Clang sysroot, zsh/nano builds, initrd packaging, and QEMU
  launchers.
- `docs/`: ABI, filesystem, synchronization, networking, and conformance notes.

## Build and run

Prerequisites include Git, CMake 3.20+, a C/C++ toolchain, Clang for
userspace, QEMU, Python 3 for helper scripts, and the archive tools used by
the test suite (`cpio`, `xxd`, `gzip`, and `zstd`). The repository contains
pinned zsh, nano, gnulib, and ncurses submodules. Initialize them with
`toolchain/init-userspace.sh` before running userspace workflows.

Build the supported kernel directly:

```sh
cmake -S kernel -B kernel/build \
  -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build kernel/build
```

Build the production AArch64 `virt` early boot stage (not a full kernel boot;
see `docs/aarch64-virt-boot.md`):

```sh
cmake -S kernel -B kernel/build/aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=kernel/cmake/toolchain-aarch64-virt.cmake \
  -DARCH=aarch64 -DPLATFORM=virt -DBOOT=virt
cmake --build kernel/build/aarch64 --target astiakrn
kernel/scripts/run-aarch64-virt-smoke.sh
```

Build the repository-level CMake project instead when working with an IDE or
the repository-level kernel project:

```sh
cmake -S . -B build -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build build
```

For CLion, open the repository root and use a CMake profile with the same
`ARCH`, `PLATFORM`, and `BOOT` cache values. The root project owns the kernel
subproject and exposes the compile database in the selected build directory.

Boot the kernel in QEMU with the supported launcher:

```sh
./kernel/scripts/run-qemu.sh
```

The launcher uses `-kernel`, `-serial stdio`, and no display. Stop the idle
kernel with `Ctrl-C`.

For a direct graphical VGA session with a GDB remote, configure the
repository-level CMake project and build the CLion-friendly target:

```sh
cmake -S . -B build -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build build --target astia_direct_vga_gdb
```

The target builds `astiakrn` and a small `newc` initrd, then invokes QEMU
without a GRUB ISO using two CPUs, 256 MiB, `-vga std`, and `-gdb tcp::1234`.
QEMU starts paused (`-S`) so GDB can attach before the first instruction:

```sh
gdb build/direct-kernel/src/astiakrn
(gdb) target remote :1234
(gdb) break kmain
(gdb) continue
```

Pass `-DASTIA_DIRECT_GDB_PORT=4321` to the CMake configure command to change
the port. Pass `-DASTIA_DIRECT_GDB_MODE=attach` to let QEMU run immediately;
the launcher also accepts `--attach` and `--non-stop`. In GDB, use
`set non-stop on` when independently debugging vCPUs in non-stop mode.

PVH provides memory, modules, command line, and ACPI handoff data, but not a
Multiboot2 framebuffer tag. The direct target therefore does not invent a
framebuffer descriptor: it keeps the graphical QEMU VGA device visible while
using the serial console and the `boot ok` marker. The existing GRUB launcher
remains the framebuffer-backed path.

## Userspace and QEMU workflows

The complete interactive zsh workflow builds the kernel test artifacts, Astia
libc/sysroot, coreutils, and the pinned zsh submodule before launching QEMU.
Initialize the upstream submodules after cloning:

```sh
toolchain/init-userspace.sh
./toolchain/run-zsh-qemu.sh
```

Generated zsh and nano files are kept in the ignored `build/userspace` cache,
so unchanged upstream sources are not rebuilt from scratch. Override the source
or cache directories when needed:

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

GNU nano is opt-in. The launcher builds the pinned nano submodule in the
reusable cache and adds it to the initrd as `/bin/nano`. If the pinned nano
submodule endpoint is unavailable, `toolchain/init-userspace.sh` verifies and
extracts the pinned GNU nano 8.6 archive instead:

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

## Network and USB foundations

The network subsystem exposes typed `netdev` objects and bounded ownership
paths for driver callbacks. The RTL8139 module exercises discovery and
transmit/reclaim fixtures, but Astia does not yet provide a TCP/IP stack or
end-to-end packet delivery. See [`docs/net-subsystem.md`](docs/net-subsystem.md).

The USB work is an experimental, bounded xHCI/HID foundation with host tests
and opt-in QEMU smoke coverage. It is not a general USB stack; class-driver
coverage and hardware support remain incomplete. See
[`modules/usb/README.md`](modules/usb/README.md).

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

The C/C++ formatting workflow checks changed `.c`, `.cpp`, `.h`, and `.hpp`
files with the repository `.clang-format` configuration. Run a full-tree check
locally with a pinned formatter, for example:

```sh
python -m pip install clang-format==18.1.8
.github/scripts/check-format.sh
```

The workflow also enforces the repository statement, brace, declaration, and
namespace layout rules: a blank line before and
after every `if`, `for`, `while`, `switch`, `do`, `try`, or bare `{ ... }`
block that stands on its own line (a block-level `return` only needs a blank line before it, and an `if` checking the variable assigned directly above (`x`, `!x`, `x == nullptr`, `x != nullptr`, `x < 0`, …) is kept attached, with any blank line between them removed), and no
braces around single-statement `if`/`else` bodies. `for`, `while`, and `do`
bodies must always be braced, and when one branch of an `if`/`else if`/`else`
chain has to keep braces, every branch in the chain gets braces. Namespace
bodies have no blank line immediately after the opening brace or before the
closing brace, and a run of consecutive declarations or assignments is
separated from the statements around it. Function and method definitions are
separated from their neighbours by a blank line; declarations and prototypes
are exempt. `struct`, `class`, `enum`, and `union` definitions follow the same
rule; forward declarations are exempt. A statement block must not be followed
by a redundant `;`; type definitions keep their terminating semicolon. In C++
sources, `for (;;)` is rewritten as `while (true)`; C sources are left alone
because `true` is not defined there. A case label stays next to its statements,
and no blank line is kept on either side of a case/default label. Apply the fixes with:

```sh
.github/scripts/check-cpp-style.py --fix path/to/file.cpp
```

Without arguments the checker inspects files changed since the main merge base;
pass `--full-tree` to check every tracked C/C++ file. Run the pinned
`clang-format` afterwards to re-indent unbraced bodies.

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

`ASTIA_BUILD_TESTS` defaults to `ON` for the repository-level CMake project,
so a fresh CLion CMake profile discovers the deterministic tests in the CTest
Application. External and interactive suites remain disabled unless their
explicit opt-in options are enabled; see [`docs/ctest.md`](docs/ctest.md).

The direct launcher smoke test checks the generated QEMU arguments, the GDB
endpoint, both paused and attach modes, and the expected serial boot marker.

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
- The network layer is a driver-facing foundation only; TCP/IP and network
  sockets are not implemented.
- The USB xHCI/HID path is experimental and its hardware smoke coverage is
  opt-in and incomplete.
- The module ABI is being consolidated around typed C++ contracts. The
  C-linkage `module_main` loader entry remains, but module headers and
  registration details are not a general source-compatibility promise; rebuild
  ET_REL modules with the matching kernel.

Near-term work is tracked in [`docs/initrd-format.md`](docs/initrd-format.md),
[`docs/ext4-kmod-design.md`](docs/ext4-kmod-design.md), and
[`kernel/README.md`](kernel/README.md): compressed initrd decoding, safe
module-facing filesystem ownership and mount APIs, broader architecture and
platform policy providers, and tighter process/AIO/mapping ownership
boundaries.

## Documentation

- [Kernel guide](kernel/README.md)
- [Module ABI](docs/module-abi.md)
- [Versioning](docs/versioning.md)
- [CTest suites](docs/ctest.md)
- [Toolchain and userspace](toolchain/README.md)
- [Man-page documentation guide](tools/man/DOCUMENTING.md)

For changes, open an issue or pull request on
[GitHub](https://github.com/arlirad/astia). Please keep claims about
architectures, hardware, and conformance aligned with the limitations above.

## License and attributions

Astia project source and metadata are released under the MIT License.

- Copyright: Arlirad, 2026
- License: MIT (`SPDX-License-Identifier: MIT`)

The complete license text is in [`LICENSE`](LICENSE). Imported and generated
materials retain their own upstream license notices. The zsh, GNU nano,
ncurses, and LTP workflows use external source trees; their upstream licenses
and notices remain applicable. The vendored zstd and miniz decoder sources in
[`third_party/`](third_party/README.md) keep their upstream BSD and MIT
licenses, reproduced in [`docs/NOTICE`](docs/NOTICE).
