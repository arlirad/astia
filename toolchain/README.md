# Astia Clang userspace toolchain

The toolchain file targets `x86_64-unknown-elf` and deliberately excludes the
host C library.  Clang's resource headers provide only compiler headers
(`stddef.h`, `stdint.h`, `stdarg.h`, ...); libc and the ABI headers come from
Astia.

## Build the libc sysroot

```sh
cmake -S libc -B /tmp/astia-clang-libc \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/astia-x86_64-clang.cmake \
  -DASTIA_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/astia-sysroot
cmake --build /tmp/astia-clang-libc
cmake --install /tmp/astia-clang-libc
```

The resulting sysroot contains:

- `include/` — Astia libc headers,
- `include/astia/api/` — kernel/userspace ABI headers,
- `lib/libastia_libc.a` — the freestanding C library.

The first userspace link still needs Astia's `crt0.S` and Clang's compiler
builtins archive.  This is intentional: startup code is an ABI component of
every freestanding userspace image, including zsh.

The current end-to-end smoke test automates the sysroot build and links the
existing libc integration image:

```sh
toolchain/build-init-libc.sh
```

## Initialize pinned userspace sources

The repository keeps clean upstream zsh and GNU nano checkouts as pinned
submodules. Initialize them after cloning:

```sh
git submodule update --init --recursive
```

The current pins are zsh `zsh-5.9.2`
(`ddee3e7a751a4f03d5a1041fd44bb7af7bc0cebb`) and nano `v8.6`
(`326e4146b2267d8683a8eb3333aa49c9023d0dbf`). To change a pin, check out a
reviewed upstream tag in the submodule and record the gitlink in the parent;
never replace it with an unreviewed branch tip. Upstream source and license
files remain in the submodules and are not copied into Astia.

## Build zsh

With the pinned checkout:

```sh
ZSH_SOURCE="$PWD/userspace/zsh" ZSH_BUILD="$PWD/build/userspace/zsh" \
  toolchain/build-zsh.sh
```

The ELF is written to `build/userspace/zsh/output/zsh`. To add it to an
existing initrd:

```sh
toolchain/package-zsh-initrd.sh \
  kernel/build-tests/tests/fixtures/boot_initrd.cpio \
  build/userspace/zsh/output/zsh \
  /tmp/astia-zsh-initrd.cpio
```

`build-zsh.sh` never edits the zsh checkout.  It supplies configure answers
through `config.site` and uses only the Astia sysroot and startup object.  In
particular, do not run the historical `patch-zsh-astia.sh` or
`patch-zsh-signals.py` helpers: they target an older zsh source layout and
their ASTIA branches bypass zsh's native fork handshake and signal wait
protocol.  Native zsh expects the libc/kernel `fork`, `waitpid`, `sigsuspend`,
process-group, and signal-restorer ABI directly.

## Launch zsh in QEMU

From a fresh checkout, the complete flow is one command:

```sh
./toolchain/run-zsh-qemu.sh
```

Every normal launch runs the incremental kernel preflight before building the
userspace image, so source changes cannot leave the default runtime stale.  It
builds the Astia libc/sysroot and native coreutils, and starts QEMU. The
default source is `userspace/zsh` and the default generated build cache is
`build/userspace/zsh`; both are outside the submodule. `ZSH_SOURCE` may name an
existing complete zsh checkout and `ZSH_BUILD` selects the reusable build
directory:

```sh
ZSH_SOURCE=/tmp/zsh ZSH_BUILD=/tmp/astia-zsh-build \
  ./toolchain/run-zsh-qemu.sh
```

The launcher never patches the zsh checkout. A source tree without generated
`configure` is copied to a private build staging directory before zsh's
bootstrap step. Configure results and compiler outputs are reused when inputs
are unchanged.

This is an interactive TTY launcher: run it directly from a terminal.  It
executes QEMU with `-serial stdio`, so stdin, stdout, and terminal signals are
connected directly to the zsh session.  Do not pipe it or redirect stdin; an
already-closed stdin makes the zsh session exit immediately.  Type `exit` to
leave zsh; QEMU remains the foreground process until it is stopped with
Ctrl-C.  `ASTIA_KERNEL` and `ASTIA_INITRD` override the default kernel
runtime and initrd paths independently.

The automated PTY checks remain available with the explicit harness opt-in:

```sh
ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

The default launcher never starts the PTY harness or requires Python.

For the GRUB/framebuffer-backed terminal, use the graphical sibling launcher:

```sh
./toolchain/run-zsh-vga-qemu.sh
```

It packages the same zsh/coreutils/initrd inputs into a Multiboot2 GRUB ISO,
opens zsh on `/dev/ttv0`, keeps diagnostics on a separate serial channel, and
requires the runtime `framebuffer: ready` marker. `ASTIA_VGA_FALLBACK=serial`
is an explicit UART-only fallback when the host bootloader does not emit a
framebuffer descriptor. The VGA smoke and capture workflow is documented in
[`docs/zsh-vga.md`](../docs/zsh-vga.md):

```sh
ASTIA_ZSH_VGA_HARNESS=1 ./toolchain/run-zsh-vga-qemu.sh
```

CLion users can configure the repository root with CMake and launch the
graphical session through the `astia_vga_qemu` target. The standalone launcher,
its smoke harness, and the CMake target all default to `-smp 2` and `-m 256M`;
configure `ASTIA_VGA_QEMU`, `ASTIA_VGA_MEMORY`,
`ASTIA_VGA_CPUS`, `ASTIA_VGA_NANO`, and the `ASTIA_VGA_*_BUILD`/`ASTIA_VGA_RUN`
paths as needed. The target invokes this launcher directly and does not
duplicate its QEMU command line.

Set `ASTIA_VGA_STRICT=1` with the harness to require the real 800x600 24-bit
BGR Multiboot2 descriptor and matching screenshot evidence.

For a clean rebuild of the kernel runtime/initrd, zsh build, and launcher work
directory, use the explicit clean mode:

```sh
ASTIA_CLEAN_BUILD=1 ./toolchain/run-zsh-qemu.sh
```

Clean mode regenerates compatible default kernel artifacts and builds an
unpatched zsh checkout before launching QEMU.  `ASTIA_KERNEL` and
`ASTIA_INITRD` remain selected when supplied; their files are not removed by a
normal launch, and are only replaced if clean mode explicitly removes their
containing build directory.  To reuse existing zsh and kernel artifacts while
still doing the normal incremental preflight, use:

```sh
ZSH_SOURCE=/tmp/zsh ZSH_BUILD=/tmp/astia-zsh-build \
ASTIA_KERNEL="$PWD/kernel/build-tests/src/astiakrn_runtime" \
ASTIA_INITRD="$PWD/kernel/build-tests/tests/fixtures/boot_initrd.cpio" \
  ./toolchain/run-zsh-qemu.sh
```

The previous launcher was not a clean build: it ran the kernel test only when
an artifact was missing, so an existing stale `astiakrn_runtime` could be
reused.

The launcher also builds the native Astia coreutils from `coreutils/` and
places them in `/bin` (`cat`, `echo`, `env`, `false`, `head`, `pwd`, `true`,
`uname`, and `wc`).

Nano packaging is opt-in so normal zsh launches do not build it. Set
`ASTIA_NANO=1` to build the pinned port automatically in the reusable
`build/userspace/nano` cache and package it as `/bin/nano`:

```sh
ASTIA_NANO=1 ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

Use these overrides when a cache or prebuilt artifact is preferred:

```sh
ASTIA_NANO=1 ASTIA_NANO_BUILD=/tmp/astia-nano-build \
  ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
ASTIA_NANO_BINARY=/tmp/astia-nano-build/output/nano \
  ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

The launcher invokes `toolchain/build-nano.sh` with the Astia sysroot and
startup object; `ASTIA_NANO_BUILD` overrides the default cache directory and
`NANO_SOURCE` overrides the pinned checkout. The default generated source and
build trees are outside `userspace/nano`. The pinned git checkout contains
`autogen.sh` but not generated `configure`; bootstrap it only with an
already-available gnulib checkout at
`f05b5a23f6cef8833402a298d1576a0118912ac8`:

```sh
NANO_GNULIB_SOURCE=/path/to/gnulib \
  ASTIA_NANO=1 ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

The bootstrap source must be obtained separately from official Savannah
(`https://git.savannah.gnu.org/git/gnulib.git`). Astia never clones or pulls
bootstrap dependencies and never follows an unpinned branch. A release tree
with generated `configure` can instead be supplied with `NANO_SOURCE`.

The launcher also provides a repeated nano/zsh terminal regression harness. It
runs `true`, then `nano passwd` and no-argument `nano`, exits each editor,
checks status 0 and prompt recovery, and rejects `bad tcgets` or `unknown
error` output:

```sh
ASTIA_NANO=1 ASTIA_NANO_HARNESS=1 ASTIA_NANO_ITERATIONS=3 \
  ASTIA_ZSH_HARNESS=1 ./toolchain/run-zsh-qemu.sh
```

For configure-based software, use the target as the host and do not let the
build system search the machine's native headers or libraries:

```sh
./configure --host=x86_64-unknown-elf \
  CC='clang --target=x86_64-unknown-elf' \
  CFLAGS='--sysroot=/tmp/astia-sysroot -ffreestanding -fno-builtin -mno-red-zone' \
  LDFLAGS='--sysroot=/tmp/astia-sysroot -nostdlib -static'
```

## Run the Open POSIX tests

The first conformance batch is sourced from LTP's external
`testcases/open_posix_testsuite` checkout. It is not copied into Astia and the
harness refuses modified or untracked source checkouts. Clone it outside this
repository, then build Astia libc and an unpatched zsh checkout:

```sh
git clone --depth=1 https://github.com/linux-test-project/ltp.git /tmp/ltp
toolchain/build-init-libc.sh
ZSH_SOURCE="$PWD/userspace/zsh" ZSH_BUILD="$PWD/build/userspace/zsh" \
  toolchain/build-zsh.sh
```

Run the initial process/signal batch under the Astia kernel and QEMU:

```sh
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh
```

The initial batch is `fork/4-1`, `fork/9-1`, `kill/2-1`, and `killpg/1-1`. The fork tests
cover process creation and the POSIX alarm reset rule; `getpid/1-1` now builds with
Astia's pthread mutex and cancellation declarations. Its getpid semantics
dependencies `sysconf`, `mkstemp`, and `ftruncate` are implemented. Astia
reports its actual limits via `sysconf`: 64 open descriptors, 64 processes,
and 100 clock ticks per second. The complete direct `kill` category, including
the cases that exercise `perror`, now builds and passes. Select one test or all
direct C tests in one Open POSIX interface directory:

```sh
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --test getpid/1-1
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --category kill
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --test kill/2-1
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --category killpg
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --test killpg/1-1
POSIX_TEST_SOURCE=/tmp/ltp toolchain/run-posix-tests.sh --list
```

The current pthread and unnamed semaphore support is active: all selected
compilable `pthread_sigmask` tests and the available process-private semaphore
cases pass against Astia. The external LTP `pthread_sigmask/15-1.c` source
omits `<pthread.h>` and is retained as a source compile blocker, not an Astia
failure. Named semaphores, process-shared semaphores, and `shm_open` remain
unsupported; their current LTP sources require unavailable declarations.
Realtime signal constants (`SIGRTMIN`/`SIGRTMAX`) and queued `sigqueue`
payloads remain unsupported, as do POSIX timers and message queues. The
`pthread_equal/2-1` stress test passes with Astia's pthread synchronization
behavior. Target-thread `pthread_kill` delivery, null-signal liveness checks,
per-thread masks, and `sigpending` are supported.

Astia's regular-file AIO read, write, cancellation, suspension, and list-I/O
cases are available. The remaining AIO blockers are `aio_fsync`, which is not
implemented, and residual socket AIO cancellation cases whose LTP expectations
or realtime signal sources do not match Astia yet. Socket-pair datagrams,
`socklen_t`, `getsockopt`, and `SO_SNDBUF` are implemented. The exact residual
source blockers and observed test defects are listed in
`toolchain/posix-expected-failures.txt`. In particular,
`pthread_spin_unlock/3-1` rejects the permitted `EPERM` result,
`pthread_cond_timedwait/2-3` unlocks a mutex from a non-owner after the owner
thread exits, and several 20-, 30-, and 100-thread cases exceed Astia's
current thread-resource limit.

`POSIX_TEST_BUILD` selects the work directory (default
`/tmp/astia-posix-tests`), and `POSIX_TEST_TIMEOUT` changes the QEMU timeout.
The work directory retains compile logs, the packaged initrd, and
`logs/qemu.log`. Each test emits `POSIX_TEST ... PASS`, `FAIL`, or
`UNSUPPORTED`; only PASS and UNSUPPORTED make the harness succeed. These are
Astia results from the cross-built binaries in QEMU, never host-POSIX results.

The suite's full conformance, functional, and stress trees are not yet
claimed. The exact first-batch scope and observed blockers are tracked in
`toolchain/posix-expected-failures.txt`; expand the batch only after building
and running a selected category against Astia.

The existing `libc_malloc_test` timeout is an unrelated verification gate and
is not changed by this harness.

## Run the noninteractive zsh regression

The regression runner does not require a TTY.  Build its inputs, then run it
with the explicit initrd path (the runner's QEMU stdin is never interactive):

```sh
kernel/scripts/test.sh kernel/build-tests
ZSH_SOURCE="$PWD/userspace/zsh" ZSH_BUILD="$PWD/build/userspace/zsh" \
  toolchain/build-zsh.sh
ASTIA_KERNEL="$PWD/kernel/build-tests/src/astiakrn_runtime" \
ASTIA_INITRD="$PWD/kernel/build-tests/tests/fixtures/boot_initrd.cpio" \
ZSH_BUILD="$PWD/build/userspace/zsh" toolchain/run-zsh-regression.sh
```
