# astiakernel

The **astia** kernel: a modular freestanding kernel project. This directory
holds the kernel itself; other components will live alongside it at the
repository root.

## Goals

- C++20, no exceptions (-fno-exceptions), no RTTI (-fno-rtti)
- Modular design: subsystems (e.g., mm/, dev/) can be replaced or swapped
- Architecture support: x86_64 production; aarch64 bring-up sources only
- Build system: CMake

## Layout

- `src/` - Core source tree
- `include/` - Public headers
- `cmake/` - CMake modules
- `scripts/` - Build/run scripts

Driver-facing contracts live under `include/astia`: device and driver
interfaces (`dev/device.hpp`, `dev/driver.hpp`, `dev/pci.hpp`,
`dev/blk/blkdev.hpp`, and `dev/chr/chrdev.hpp`), MMIO ownership
(`dev/mmio.hpp`), boot handoff
parsers (`boot/virt/`), and user address policy (`proc/user_layout.hpp`).
Architecture instruction implementations and subsystem internals remain under
`src/`.

## Memory layout (x86_64)

Provided per arch by `astia::arch` (`include/astia/arch/layout.hpp`,
values in `src/arch/<arch>/layout.cpp`):

- `kernel_base = 0xFFFF800000000000` - start of the canonical higher half (PML4 slot 256, PDPT index 0); also the base
  of the direct physical map (`kernel_base + p` maps physical `p`).
- `image_base = 0xFFFFFFFF80000000` - top 2 GiB (PML4 slot 511, PDPT index
  510), where the kernel image links: the image lives at `image_base +
  physical address`, which keeps static code/data reachable with
  `-mcmodel=kernel` sign-extended addressing. Must match
  `cmake/linker/virt-x86_64.ld` and the boot page tables in `boot.S`.
- `recursive_base = 0xFFFFFF0000000000` - window of PML4 slot 510, which
  every page map reserves for itself: the entry points back at the map's own
  root (recursive mapping), so the active address space's tables are
  addressable by arithmetic on virtual addresses and table access never
  depends on a 1:1 identity mapping. Non-active maps reach their table
  frames through the direct map instead; the identity window is only used
  before the first `activate()` (and in host unit tests).

Boot installs identity + higher-half windows over the first 1 GiB; the
`paging` init step then builds and activates the kernel `page_map`
(identity window, direct map over all usable RAM, the image at
`image_base`) and publishes the heap window (right after the image) for
the `heap` step, which backs it with frames via `mm::heap`'s mapper
callback.

## Interrupts (x86_64)

Provided by `astia::arch` (`include/astia/arch/interrupts.hpp`,
implementation in `src/arch/x86_64/interrupts.cpp`); the `interrupts`
init step arms delivery but leaves interrupts masked — opening the gate (`cpu::interrupts()`) is the caller's decision.

- **IDT** — all 256 vectors point at entry stubs generated at build time (`gen_isrs.sh` → `isrs_gen.S`), which normalise
  every frame to
  `[vector][error]` and share one common path (`isrs.S`). Fatal
  exceptions dump vector/name/error/RIP (plus CR2 for `#PF`) over serial
  and end the boot via `debug_exit`, so a crash is a readable report
  rather than a triple fault.
- **PIC** — both 8259s are remapped so IRQs arrive as vectors 32..47,
  and stay fully masked; each tick source un-masks only what it needs.
- **Timer** — the tick comes from the local APIC timer (per-CPU, so it
  scales to SMP, unlike the PIT): its frequency is measured against a
  ~50 ms PIT one-shot at bring-up, then it runs periodically at 100 Hz
  on vector 48. When the LAPIC is unusable (absent, disabled, or x2APIC
  mode) the PIT remains the tick source with IRQ0 unmasked;
  `current_timer_source()` reports which one is live.
- **`interrupt_guard`** (`cpu.hpp`) — RAII mask of the interrupt gate,
  the counterpart to `core::spinlock` for critical sections that an
  interrupt handler might re-enter: mask before locking, restore after
  unlocking.

The lock hierarchy, IRQ restrictions, and no-callback-under-lock rule are
documented in [`SYNCHRONIZATION.md`](SYNCHRONIZATION.md).  It is a partial
hierarchy: subsystem boundaries should be crossed using references or copied
state rather than by nesting unrelated locks.

## Building

For CLion/clangd, open the repository root and use its CMake profile.  The
root `CMakeLists.txt` owns the kernel subproject and exports the compile
database in the selected build directory; this keeps `kernel/include` in the
commands used to index headers such as `<astia/fs.hpp>`.

The kernel can still be configured directly from this directory (`kernel/`):

```bash
cmake -S . -B build -DARCH=x86_64
cmake --build build
```

When configuring from the repository root, use `cmake -S . -B build` instead.
Do not point clangd at a stale `kernel/cmake-build-debug` database after
changing the CLion CMake profile; reload CMake (or delete that build
directory) so the generated `compile_commands.json` is refreshed.

The only supported kernel target is `ARCH=x86_64 PLATFORM=virt BOOT=virt`. The
`ARCH=aarch64` production configuration is rejected, including when
`ASTIA_EXPERIMENTAL_AARCH64=ON`; the incomplete AArch64 tree has no boot path,
linker layout, or kernel architecture contract. To compile its current source
stubs as an explicitly separate probe, configure an x86_64 build with
`-DASTIA_EXPERIMENTAL_AARCH64=ON` and an AArch64 compiler in
`ASTIA_AARCH64_CXX_COMPILER`, then build the opt-in target
`astia_experimental_aarch64`. That target is compile-only, excluded from the
default build, and is never linked into `astiakrn`, `astiakrn_test`, or
`astiakrn_runtime`.
The linker script is selected from that target as
`cmake/linker/${BOOT}-${ARCH}.ld` and passed to every kernel executable; do not
configure a separate linker-script cache variable.
The stackful `core::fiber` implementation is experimental and excluded from
production builds; enable `-DASTIA_BUILD_EXPERIMENTAL_FIBER=ON` only for
focused architecture experiments.

The remaining audit findings are tracked for later slices: the generic MMIO
window and user virtual-address layout still need arch/platform policy
providers; process, AIO, and mapping code need narrower ownership boundaries;
fixed CPU/resource capacities need centralized policy; intentional stubs need
an explicit inventory; and the existing production warnings need to become
errors after their allocation and ownership fixes.

## TTY foreground process-group contract

The libc `tcgetpgrp()` and `tcsetpgrp()` wrappers are deliberately thin:
they issue `ioctl(fd, TIOCGPGRP, &pid)` and `ioctl(fd, TIOCSPGRP, &pid)` and
preserve the kernel error (`EBADF`, `EFAULT`, `ENOTTY`, `EINVAL`, etc.).  No
new direct libc syscall is implied by these functions.

The current tty field is only storage for a foreground process-group ID.  The
process subsystem now owns process-group/session identity; the tty layer still
needs the following kernel-side validation and job-control contract:

* every process has a `pgrp` ID (initially its PID), and the kernel can resolve
  an ID to a live process group;
* `TIOCGPGRP` returns the tty's live foreground `pgrp` ID, or `-ENOTTY` for a
  non-terminal and `-EBADF` for an invalid descriptor;
* `TIOCSPGRP` accepts only a live group in the caller's session, otherwise
  returns `-EINVAL` (or `-EPERM` when the session/ownership check fails);
* a tty read by a member of a non-foreground group sends `SIGTTIN` to that
  group and does not perform the read.  If the group blocks or ignores the
  signal, the operation instead fails with `-EIO`;
* a tty write by a non-foreground group sends `SIGTTOU` when `TOSTOP` is set
  and does not perform the write.  If blocked or ignored, it fails with
  `-EIO`; `tcsetpgrp()` itself follows the same `SIGTTOU` rule.

Signal delivery must target the complete process group, and the check must be
made by the kernel before the tty operation.  Until the tty layer consumes
that process API, its stored ID must not be treated as proof that a group
exists or as a reason to synthesize process-group IDs in libc.

## Running

```bash
./scripts/run-qemu.sh
```

This builds the kernel and boots it with QEMU's `-kernel` loader:

```bash
qemu-system-x86_64 -kernel build/src/astiakrn \
    -serial stdio -display none -monitor none -no-reboot
```

The x86-64 image is booted via the x86/HVM ("PVH") direct boot ABI: the
entry point is published in a `.note.Xen` ELF note, which QEMU reads when
`-kernel` is given. (QEMU's multiboot loader only accepts i386 images, and
the Linux boot protocol needs a bzImage, so neither fits a small 64-bit
kernel.) QEMU enters `_start` in 32-bit protected mode; `src/boot/virt/boot.S`
sets up identity and higher-half page tables, switches to long mode and
calls `kmain()` in the linked-high image.

On success the kernel writes `OK` to COM1 (0x3F8), which QEMU routes to
stdio. The kernel then idles; press `Ctrl-C` to stop QEMU.

## Direct VGA/GDB developer target

The repository-level CMake project exposes `astia_direct_vga_gdb` for CLion
and other IDEs. It builds the PVH kernel and a small initrd, then launches
QEMU directly with no GRUB ISO:

```sh
cmake --build build --target astia_direct_vga_gdb
```

The default command uses `-smp 2 -m 256M -vga std -display gtk`, routes the
serial console to the terminal, and listens on `tcp::1234`. It includes `-S`,
so attach with the matching kernel ELF before continuing:

```sh
gdb build/direct-kernel/src/astiakrn
(gdb) target remote :1234
```

Use `-DASTIA_DIRECT_GDB_PORT=4321` for another port. Configure
`-DASTIA_DIRECT_GDB_MODE=attach`, or invoke the launcher with `--attach` or
`--non-stop`, to omit `-S` and attach to a running guest. GDB's own
`set non-stop on` controls per-vCPU debugger behavior.

PVH has no Multiboot2 framebuffer handoff. This target deliberately reports
that the framebuffer descriptor is unavailable and uses the UART fallback;
it does not synthesize dimensions or a physical address. Use the preserved
GRUB launcher for a real framebuffer-backed TTV session.
