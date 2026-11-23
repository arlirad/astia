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
7. Build a bounded, static identity-map page-table arena from the typed
   handoff, install it in `TTBR0_EL1`, and enable stage 1 translation.
8. Print the post-MMU marker, MAIR/TCR/SCTLR state, and a PL011 write/read
   marker, then halt through PSCI `SYSTEM_OFF`.

Explicit non-goals: scheduler, full virtual-memory management, user address
spaces, cache maintenance policy, GIC/IRQ, timers, modules, userspace, SMP,
initrd, ACPI, and full generic-kernel boot.

## Direct ELF boot contract

QEMU documents, under "Hardware configuration information for bare-metal
programming" (<https://www.qemu.org/docs/master/system/arm/virt.html>), that:

- RAM starts at `0x4000_0000`.
- A bare-metal guest (any non-ELF image or an ELF image not using the Linux
  Image protocol) finds the DTB at the start of RAM.
- Only flash (`0x0`) and RAM (`0x4000_0000`) may be hard-coded; other device
  addresses must come from the DTB.

Direct inspection of QEMU `hw/arm/boot.c` and `hw/arm/virt.c` additionally
shows that an ELF image sets `is_linux = 0`, is entered directly at the ELF
entry point, receives no firmware reset, `x0` is not a DTB pointer, and no
stack is supplied.

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
the base of RAM intact. A LOAD below `0x4010_0000` is rejected by the kernel
smoke runner because it can overlap that DTB.

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
size == 0` entry before the next DTB block starts. Static `/reserved-memory`
child `reg` ranges are also translated. Dynamic allocations (`size`/`alignment`/`alloc-ranges`) are not
implemented. No allocator exists in this stage, so reserved ranges are
reported, not acted on.

## Platform ownership

`kernel/src/plat/aarch64-virt/` owns the PL011 polled transmitter, PSCI
`SYSTEM_OFF`, and the trusted PL011 base/window policy. There is no separate
standalone PL011 implementation: the production QEMU runner boots the
production `astiakrn` image through its normal ELF entry and DTB path.

## MMU / identity-map policy

This milestone adds the first production AArch64 stage 1 translation. It is an
early-boot identity map, not a general virtual-memory manager:

- 4KB translation granule, initial lookup at level 0.
- 48-bit lower VA range (`TCR_EL1.T0SZ = 16`), translated through `TTBR0_EL1`.
- 40-bit PA policy (`TCR_EL1.IPS = 0b010`), which covers every address used by
  the QEMU `virt` image, DTB, RAM, and PL011 and is within the architecturally
  supported `ID_AA64MMFR0_EL1.PARange` space. Every supplied VA/PA is checked
  against the policy bounds before a descriptor is written.
- `TTBR1_EL1` is not used (`TCR_EL1.EPD1 = 1`).
- All table frames come from a statically allocated, 4KiB-aligned,
  256-table (1MiB) arena in `.bss`. Allocation is zeroed and checked; the
  arena is intentionally not a generic frame allocator and is not reusable.

### Exact register policy

| Register | Value / fields | Reason |
| --- | --- | --- |
| `MAIR_EL1` | `0x00000000AA44FF04` | Attr0 `0x04` = Device-nGnRE, Attr1 `0xff` = Normal Inner/Outer Write-Back Read-Allocate Write-Allocate, Attr2 `0x44` = Normal Non-cacheable (`cache_disable`), Attr3 `0xaa` = Normal Write-Through (`write_through`) |
| `TCR_EL1` | `0x0000000200903510` | T0SZ=16, T1SZ=16, EPD1=1, IRGN0=01, ORGN0=01, SH0=11, TG0=00, IPS=010, AS=0, A1=0 |
| `TTBR0_EL1` | page-table root + ASID 0, CnP=0 | Lower VA range; no ASID use in this stage |
| `TTBR1_EL1` | 0 | Upper range disabled by `EPD1` |
| `SCTLR_EL1` | M=1, C=1, I=1; WXN=0, EE=0, E0E=0 | Enable MMU and caches; do not turn writable image pages into execute-never; little-endian |

`TCR_EL1.SH0 = 0b11` makes translation-table walks Inner Shareable;
`IRGN0/ORGN0 = 0b01` makes the tables Normal Write-Back cacheable. Device
translation-table memory is not used.

### Descriptor policy

Table descriptors are `next_table[47:12] | table(bit 1) | valid(bit 0)`.
`APTable`, `XNTable`, and `UXNTable` are zero, so the hierarchy adds no
permission restrictions; `TCR_EL1` leaves hierarchical permissions enabled
with no restrictions at table level. Leaf descriptors set AF=1 (the stage does
not use hardware AF management), nG=0 for all current kernel mappings, and:

| Mapping | AttrIndx | SH | AP[2:1] | PXN | UXN |
| --- | --- | --- | --- | --- | --- |
| Usable RAM default | 1 (Normal WBWA) | 11 (Inner Shareable) | 00 (privileged RW) | 1 | 1 |
| Image (code + data + BSS + arena) | 1 | 11 | 00 (privileged RW) | 0 | 1 |
| DTB window | 1 | 11 | 10 (privileged RO) | 1 | 1 |
| Stack | 1 | 11 | 00 (privileged RW) | 1 | 1 |
| PL011 MMIO | 0 (Device-nGnRE) | 00 | 00 (privileged RW) | 1 | 1 |

`page_flags::device` selects Attr0 and forces PXN=UXN=1 in the backend.
`page_flags::no_execute` sets both PXN and UXN for Normal mappings.
`page_flags::user` sets AP[1] and nG; `writable` selects AP[2]=0. The shared API
has no executable-positive flag, so “device is non-executable” is backend
policy rather than a rejected flag combination. x86_64 deliberately treats
`page_flags::device` as a no-op; its mappings and ABI are unchanged.

RAM is mapped first with 2MiB Normal blocks (NX), then the DTB, image, and
stack ranges are installed with 4KiB leaves; the image leaves are executable
because text and data currently share pages. The PL011 is a single 4KiB
Device-nGnRE page. Identity is required for the current PC, SP, page tables,
DTB, and PL011 across the enable transition.

### Enable sequence

Tables are written with the MMU off using physical addresses that are also
identity virtual addresses. Activation then performs:

1. `MSR MAIR_EL1`, `MSR TCR_EL1`, `MSR TTBR0_EL1`, `MSR TTBR1_EL1`.
2. `DSB ISH` (table and system-register writes complete).
3. `ISB` (context synchronization for the register writes).
4. `TLBI VMALLE1IS` (discard any stale EL1&0 stage 1 entries).
5. `DSB ISH`, then `ISB`.
6. Read `SCTLR_EL1`, set M/C/I, clear WXN/EE/E0E, write it back.
7. `ISB`; the next instruction fetch and data access use the new map.

The stage performs no cache maintenance instructions: with translation
disabled, its data accesses are Normal Outer Shareable Inner/Outer
Non-cacheable, and `DSB ISH` orders the table writes before the enable. Future
cacheable-table updates must add the appropriate cache maintenance and
break-before-make/TLBI rules.

**Early-stage limitation:** map mutation after `activate()` is unsupported.
`map_4k`, `map_2m`, `unmap`, and `protect_4k` may be called only while the map
is being constructed with the MMU off. When active, the backend orders a
`TLBI VAE1IS` with a preceding `DSB ISH` and a following `DSB ISH`/`ISB` for
a single changed 4KiB VA. It still does not implement break-before-make, does
not invalidate an entire 2MiB block when splitting a Block descriptor, and
does not provide the cache maintenance that a later cacheable-table update
would require. Adding those rules is required before using this backend for
runtime mappings.

### Specification provenance

The requested `D4.2.x`/`D4.3.3` numbering corresponds to the current ARM DDI
0487 M.c numbering, where the AArch64 VMSA chapter is D8. The implementation
was written against the complete sections, not summaries:

- D8.1 address translation, D8.1.6 implemented PA size, D8.1.7 output address
  size, D8.1.9 input address size (`T0SZ`/`T1SZ`).
- D8.2.1 translation table walk, D8.2.3 TTBR base register, D8.2.4 TTBR0/TTBR1
  selection, D8.2.6 translation-walk ordering and `DSB` visibility.
- D8.2.8 VMSAv8-64 4KB granule (lookup levels, initial lookup level, descriptor
  address calculation), D8.2.12 effects of disabling translation, D8.2.13
  address translation instructions.
- D8.3.1 VMSAv8-64 table/block/page descriptor formats.
- D8.4.1 stage 1 permissions (`AP[2:1]`), D8.4.4 execute-never semantics.
- D8.5.1 Access flag management.
- D8.6.1 stage 1 memory type/Cacheability, D8.6.2 stage 1 Shareability.
- D8.17.1 break-before-make, D8.17.8 ordering/completion of TLB maintenance.
- D24.2.126 `MAIR_EL1`, D24.2.174 `SCTLR_EL1`, D24.2.193 `TCR_EL1`,
  D24.2.208 `TTBR0_EL1`; B2.6.1 `ISB`, B2.6.9 `DSB`; C5.5.77
  `TLBI VMALLE1IS`.

Source: *Arm Architecture Reference Manual for A-profile architecture*, ARM
DDI 0487 M.c, <https://developer.arm.com/documentation/ddi0487/mc>. QEMU
`virt` bare-metal contract: RAM at `0x4000_0000`, DTB at the start of RAM for
bare-metal images, all other device addresses from the DTB,
<https://www.qemu.org/docs/master/system/arm/virt.html>.

## Verification

```sh
# Focused parser tests (host)
cmake -S kernel -B kernel/build-tests -G Ninja \
  -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
  -DASTIA_BUILD_TESTS=ON -DASTIA_CROSS_USERSPACE=OFF
cmake --build kernel/build-tests --target astia_aarch64_fdt_test
ctest --test-dir kernel/build-tests -R '^astia_aarch64_fdt_test$' \
  --output-on-failure

# Focused AArch64 descriptor-policy and bounded-arena tests (host)
cmake --build kernel/build-tests --target astia_aarch64_page_map_test
ctest --test-dir kernel/build-tests -R '^astia_aarch64_page_map_test$' \
  --output-on-failure

# x86 page-map regression (device is a documented no-op)
cmake --build kernel/build-tests --target astia_page_map_test
ctest --test-dir kernel/build-tests -R '^astia_page_map_test$' \
  --output-on-failure

# Production AArch64 early boot + MMU smoke
kernel/scripts/run-aarch64-virt-smoke.sh
```

The smoke runner also asserts the post-MMU marker, the exact MAIR/TCR values,
`SCTLR_EL1.M/C/I`, and a PL011 write followed by a PL011 read.

Kernel CI (`.github/workflows/kernel-ci.yml`) runs the focused host parser
test, the production AArch64 QEMU smoke, and the existing x86 build/test
pipeline.

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

- No full kernel boot, user address spaces, virtual-memory allocator, GIC/IRQ,
  timer, modules, userspace, SMP, initrd, or ACPI.
- The identity map is deliberately static and bounded. It is not a frame
  allocator, cannot be reused for process address spaces, and does not
  implement break-before-make or cache maintenance for later table updates.
- `page_map::create(parent)` deep-clones the parent's current tables in this
  stage; later parent updates are not shared. Process address spaces are out
  of scope.
- The image is mapped privileged RWX because current linker sections share
  pages; W^X section mapping is left to a later stage.
- EL1 only; no EL2/EL3 normalization.
- FDT version 17+ only; version 16 backwards compatibility is not implemented.
- Only static reserved-memory `reg` ranges; no dynamic reserved allocations.
- Console candidate/alias tables are fixed-capacity; excess entries fall back
  to the platform default.
- Device-tree paths longer than `console_path_capacity` are rejected.
