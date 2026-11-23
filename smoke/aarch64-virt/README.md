# AArch64 QEMU `virt` direct ELF smoke

This directory contains a standalone, bootable AArch64 ELF image for QEMU's
`virt` machine. It exists only to prove the direct `-kernel` ELF boot contract
and polled early serial output. It is not part of the production kernel, does
not include production kernel headers or build targets, and does not touch the
x86 paths.

## Scope

* Build a freestanding AArch64 ELF with its own entry code, stack, linker
  script and cross-toolchain configuration.
* Boot it with `qemu-system-aarch64 -machine virt -kernel ...`.
* Emit a fixed banner through the QEMU `virt` PL011 UART using polled TX.
* Report the observed incoming exception level, `x0`, `sp`, `DAIF`, and DTB
  handoff, including bounded DTB totalsize validation.
* Verify the result with `run-qemu-smoke.sh`.

Non-goals for this milestone: kernel integration, MMU/page tables, caches,
interrupts, GIC, timers, modules, userspace, SMP, device-tree parsing, and any
change to x86 or production kernel paths.

## Verified boot contract

Sources used for the contract:

* QEMU `virt` board documentation:
  <https://www.qemu.org/docs/master/system/arm/virt.html>, section
  "Hardware configuration information for bare-metal programming". It states
  that a non-ELF image passed to `-kernel` uses the Linux kernel boot protocol
  and receives the DTB in `x0` for 64-bit guests, while a bare-metal boot gets
  the DTB at the start of RAM (`0x4000_0000`). An ELF image is bare-metal, not
  the Linux Image protocol.
* QEMU `hw/arm/boot.c`,
  <https://github.com/qemu/qemu/blob/v11.1.3/hw/arm/boot.c>,
  `arm_setup_direct_kernel_boot()` and `do_cpu_reset()`:
  an ELF load sets `is_linux = 0`; reset jumps directly to the ELF entry point
  and does not run `arm_emulate_firmware_reset()` or the AArch64 bootloader
  stub that would place the DTB in `x0`.
* QEMU `hw/arm/virt.c`,
  <https://github.com/qemu/qemu/blob/v11.1.3/hw/arm/virt.c>,
  `virt_machine_done()` and the `virt` machine init:
  `loader_start` is `0x4000_0000` and the generated DTB is installed at
  `info->dtb_start`.
* Linux `Documentation/arch/arm64/booting.rst`,
  <https://github.com/torvalds/linux/blob/master/Documentation/arch/arm64/booting.rst>,
  is referenced only to contrast the Linux Image protocol (`x0 = DTB`, EL2/EL1
  entry) with this raw ELF path. The Linux Image header rules do not apply to
  this image.

Observed on QEMU 11.1.3 with the command in `run-qemu-smoke.sh`:

| Property | Observed value |
| --- | --- |
| ELF type | ELF64 little-endian, `EM_AARCH64`, `ET_EXEC` |
| ELF entry | `0x4040_0000` |
| First LOAD physical address | `0x4040_0000` |
| Incoming exception level | EL1h (`CurrentEL` bits 3:2 = 1) |
| Incoming `x0` | `0` |
| Incoming `PSTATE.DAIF` | all masked |
| `SCTLR_EL1` | `0x00c5_0838` (MMU and caches off) |
| Incoming `SP` | `0` (QEMU does not supply a stack) |
| DTB location | `0x4000_0000`, magic `0xd00d_feed` |
| DTB size on this QEMU | `0x0010_0000` (1 MiB) |
| Accepted DTB totalsize | `0x0000_0100` through `0x0010_0000` |

The smoke establishes its own stack and zeroes `.bss` in `entry.S`. It does not
need EL normalization: the image is valid at EL1, and the QEMU command disables
EL3 and EL2 (`secure=off,virtualization=off`), so reset enters at EL1h.

### DTB placement is conditional

`virt` installs the DTB at the base of RAM only if it fits below the ELF image.
QEMU 11.1.3's `virt` DTB is 1 MiB, so an image linked at `0x4008_0000` leaves
only 512 KiB and QEMU silently omits the DTB. This image therefore links at
`0x4040_0000`, leaving the observed 1 MiB DTB window intact. The smoke checks
the DTB magic and totalsize at `0x4000_0000` rather than assuming a successful
handoff: `PASS` requires magic `0xd00d_feed` and totalsize in
`[0x100, 0x10_0000]`, and `run-qemu-smoke.sh` independently parses the reported
size and enforces the same bound. The 256-byte lower bound rejects a truncated
or header-only blob while staying far below the observed QEMU DTB size.

### PL011 address limitation

The PL011 is hard-coded at `0x0900_0000`, the QEMU `virt` UART address. QEMU's
documentation guarantees only flash at `0x0` and RAM at `0x4000_0000`; other
device addresses should be read from the DTB. Parsing the DTB is deliberately
deferred beyond this first milestone. The address is isolated in
`src/board.hpp`.

### Exit

The smoke calls PSCI `SYSTEM_OFF` with `HVC #0` only to let the QEMU test
process exit cleanly. QEMU's default PSCI conduit for this machine is HVC.
This is test-harness termination, not kernel PSCI or interrupt support.

## Build and run

From the repository root:

```sh
smoke/aarch64-virt/run-qemu-smoke.sh
```

The script configures a standalone CMake project with
`cmake/aarch64-virt-elf.cmake`, builds
`build/astia-aarch64-virt-smoke.elf`, checks the ELF machine, entry point, and
load addresses, then boots QEMU and verifies the serial output and exit status.

Overrides: `ASTIA_AARCH64_VIRT_BUILD`, `ASTIA_AARCH64_VIRT_TIMEOUT`, `QEMU`,
`READELF`, and `ASTIA_AARCH64_VIRT_CXX_COMPILER`. The cross compiler defaults
to `aarch64-linux-gnu-g++`.
