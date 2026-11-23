# AArch64 QEMU virt early boot stage

This document describes the production AArch64 `virt` early boot target added
for `ARCH=aarch64 PLATFORM=virt BOOT=virt`. It is deliberately a **stage**, not
a full kernel boot. The generic `kernel/src/kmain.cpp` graph requires paging,
IRQ/GIC, devices, and the scheduler, none of which exist for AArch64 in this
milestone.

## Target and boundary

The target is built through the normal kernel selection:

```sh
cmake -S kernel -B kernel/build/aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=kernel/cmake/toolchain-aarch64-virt.cmake \
  -DARCH=aarch64 -DPLATFORM=virt -DBOOT=virt
cmake --build kernel/build/aarch64 --target astiakrn
```

`kernel/src/CMakeLists.txt` gives AArch64 its own composition: `lib`,
`arch/aarch64`, `plat/aarch64-virt`, and `boot/aarch64-virt`. It returns before
the generic full-kernel target graph, so x86 source composition and target
behavior are unchanged. The minimal `arch_aarch64` target owns the stage start
assembly (`kernel/src/arch/aarch64/start.S`) and is linked into the production
boot target. The C++ entry symbol is `astia_aarch64_early_main`, not the generic
`kmain`. QEMU virt device code remains under `kernel/src/plat/aarch64-virt/`.

The stage does exactly this:

1. Establish a stack and clear `.bss` in `src/arch/aarch64/start.S`.
2. Validate that the CPU entered at EL1.
3. Locate the QEMU bare-metal DTB at the base of RAM.
4. Parse and validate the DTB into typed `astia::boot::aarch64::virt::boot_info`.
5. Resolve the boot console from `/chosen/stdout-path` and `arm,pl011`.
6. Print the DTB size, RAM ranges, reserved ranges, console, and handoff status
   over the PL011.
7. Halt through PSCI `SYSTEM_OFF`.

Explicit non-goals: scheduler, MMU/page tables, caches, GIC/IRQ, timers,
modules, userspace, SMP, initrd, ACPI, and full generic-kernel boot.

## Direct ELF boot contract

QEMU documents, under "Hardware configuration information for bare-metal
programming" (<https://www.qemu.org/docs/master/system/arm/virt.html>), that:

- RAM starts at `0x4000_0000`.
- A bare-metal guest (any non-ELF image or an ELF image not using the Linux
  Image protocol) finds the DTB at the start of RAM.
- Only flash (`0x0`) and RAM (`0x4000_0000`) may be hard-coded; other device
  addresses must come from the DTB.

The PR105 investigation recorded in `smoke/aarch64-virt/README.md` additionally
checked QEMU `hw/arm/boot.c` and `hw/arm/virt.c`: an ELF image sets
`is_linux = 0`, is entered directly at the ELF entry point, receives no
firmware reset, `x0` is not a DTB pointer, and no stack is supplied.

Observed on QEMU 11.1.3 with
`-machine virt,secure=off,virtualization=off -cpu cortex-a57`:

| Property | Value |
| --- | --- |
| ELF entry / first LOAD | `0x4040_0000` |
| Incoming exception level | EL1h |
| Incoming `x0` | `0` |
| Incoming `sp` | `0` (the stage supplies its own) |
| DTB location | `0x4000_0000` |
| DTB totalsize | `0x0010_0000` (1 MiB) |

The image is linked at `0x4040_0000`, leaving the observed 1 MiB DTB window at
the base of RAM intact. A LOAD below `0x4010_0000` is rejected by the smoke
runner because it can overlap that DTB.

## DTB handling

The parser is native Astia C++20 in
`kernel/src/boot/aarch64-virt/fdt.cpp`. It translates only supported fields
into `kernel/include/astia/boot/aarch64/virt.hpp`:

- `dtb`, `dtb_size`
- usable RAM ranges from `/memory` `reg`, but only for nodes whose
  `device_type` is exactly `memory`
- reserved ranges from the memory reservation block and static
  `/reserved-memory` children
- boot console base/size/source/path from `/chosen/stdout-path`
  (or deprecated `linux,stdout-path`) and `/aliases`

The DTB is untrusted input. The parser enforces:

- header magic, `totalsize`, version `>= 17` with compatible version `>= 16`
- every block offset/size bounded by `totalsize` and the supplied 1 MiB window
- header, reservation-map, structure, and strings regions checked for overlap;
  the reservation scan is bounded by the next block start, so a missing
  terminator cannot consume the structure or strings block
- structure block `4`-byte alignment and reservation block `8`-byte alignment
- one root node, balanced `FDT_BEGIN_NODE`/`FDT_END_NODE` framing
- exactly one trailing `FDT_END` at the end of the structure block
- depth cap 64 and a token budget derived from `size_dt_struct`
- property lengths, padding, and name offsets bounded by the relevant block
- NUL-terminated property/node names within the strings/structure blocks
- node-name character rules and path-capacity limits
- `#address-cells`/`#size-cells` limited to the supported 1–2 cell width for
  translated `reg` properties, with checked base+size overflow
- root `reg` ignored before any parent-frame lookup
- maximum RAM/reserved/alias/console-candidate counts

All big-endian reads use Astia's existing `astia::core::be<T>::load(const void*)`
and `store(void*)`. `core::endian.hpp` copies bytes one at a time before
converting, so the parser never casts DTB byte pointers to packed structs and
is safe for unaligned blobs. The host test includes an unaligned-blob case.

Specification sources used:

- Devicetree Specification, chapter 5, "Flattened Devicetree (DTB) Format":
  header fields, memory reservation block, structure block tokens
  (`FDT_BEGIN_NODE`, `FDT_END_NODE`, `FDT_PROP`, `FDT_NOP`, `FDT_END`), strings
  block, tree framing, and alignment.
  <https://www.devicetree.org/specifications/>
- Devicetree Specification, chapter 2, "Node Names":
  `<node-name>@<unit-address>` character rules.
- Devicetree Specification, chapter 3, "Device Node Requirements":
  root `#address-cells`/`#size-cells`, `/aliases`, `/memory`, `/reserved-memory`,
  and `/chosen` `stdout-path`.
- QEMU `virt` documentation, "Hardware configuration information for
  bare-metal programming".
  <https://www.qemu.org/docs/master/system/arm/virt.html>

### Console selection and PL011 fallback

QEMU's generated DTB identifies the UART through
`/chosen/stdout-path = "/pl011@9000000"` and
`/aliases/serial0 = "/pl011@9000000"`. The parser resolves a direct path or an
alias, finds the matching node with `compatible = "arm,pl011"`, and reads its
`reg`. A candidate is accepted only when its base exactly matches the trusted
platform value `0x0900_0000` and its `reg` size is at least the full PL011
register window `0x1000`. The parser never chooses an arbitrary MMIO address
from the DTB. If resolution fails or the candidate is hostile/undersized, the
stage reports `source=platform-default` and uses the documented QEMU fallback
`0x0900_0000` from `kernel/src/plat/aarch64-virt/board.hpp`. The fallback and
the minimum window are isolated in the platform layer.

### Reserved memory

The memory reservation block is required to be terminated by an `address == 0,
size == 0` entry before the next DTB block starts. Static `/reserved-memory` child `reg` ranges are also
translated. Dynamic allocations (`size`/`alignment`/`alloc-ranges`) are not
implemented. No allocator exists in this stage, so reserved ranges are
reported, not acted on.

## Platform ownership and PR105 smoke

`kernel/src/plat/aarch64-virt/` owns the PL011 polled transmitter and PSCI
`SYSTEM_OFF`. The retained standalone PR105 smoke
(`smoke/aarch64-virt/run-qemu-smoke.sh`) compiles those same production
sources instead of keeping a duplicate PL011 implementation. Its existing
serial contract remains unchanged and it continues to verify the raw ELF/DTB
handoff independently.

## Verification

```sh
# Focused parser tests (host)
cmake -S kernel -B kernel/build-tests -G Ninja \
  -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
  -DASTIA_BUILD_TESTS=ON -DASTIA_CROSS_USERSPACE=OFF
cmake --build kernel/build-tests --target astia_aarch64_fdt_test
ctest --test-dir kernel/build-tests -R '^astia_aarch64_fdt_test$' \
  --output-on-failure

# Production AArch64 early boot smoke
kernel/scripts/run-aarch64-virt-smoke.sh

# Retained PR105 smoke
smoke/aarch64-virt/run-qemu-smoke.sh

# x86 target composition regression
kernel/scripts/check-x86-target.sh
```

The CI workflow `.github/workflows/aarch64-virt-smoke.yml` builds and runs the
focused host parser test, both QEMU smokes (production and retained PR105), and
the x86 configure/build regression.

## Provenance

`libafxdt` (sibling repository) was reviewed read-only. It was not embedded,
copied, imported, or linked. Its parser is structurally unbounded for boot
input (single magic check, no block/offset/token bounds, recursive node sizing,
no depth cap, undefined `fdt_getnode`).

The Astia parser is an independent bounded implementation. No libafxdt code
was reused. The only shared material is specification-defined data: the DTB
magic, header field layout, and token constants. Astia's own
`astia::core::be<T>` is used for byte-order conversion. Any future selective
lift must be recorded here with the exact source and rework.

## Remaining gaps

- No full kernel boot, paging/MMU transition, GIC/IRQ, timer, modules,
  userspace, SMP, initrd, or ACPI.
- EL1 only; no EL2/EL3 normalization.
- FDT version 17+ only; version 16 backwards compatibility is not implemented.
- Only static reserved-memory `reg` ranges; no dynamic reserved allocations.
- Console candidate/alias tables are fixed-capacity; excess entries fall back
  to the platform default.
- Device-tree paths longer than `console_path_capacity` are rejected.
