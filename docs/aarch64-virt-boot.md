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
   marker.
9. Install the 2 KiB-aligned EL1 vector table in `VBAR_EL1`.
10. Execute a deliberate `brk #0x1234`, report the synchronous BRK64
    exception through the C++ fatal reporter, and halt through PSCI
    `SYSTEM_OFF`.

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

## Synchronous exception entry (BRK64)

This milestone adds the first production AArch64 exception path. It is a
fatal diagnostic path only: there is no exception return, recovery, IRQ/FIQ,
or SError handling.

### Vector table layout and VBAR_EL1

The table is the architectural 16-slot AArch64 layout: four exception sources
(synchronous, IRQ, FIQ, SError) in four groups (current EL with `SP_EL0`,
current EL with `SP_ELx`, lower EL using AArch64, lower EL using AArch32).
Each slot is 128 bytes, so the table is 2 KiB. `VBAR_EL1.VBA` occupies bits
`[63:11]` and bits `[10:0]` are RES0, so the table must be 2 KiB aligned.
The table is placed in `.vectors`, `KEEP`-retained in the executable `.text`
segment, and is therefore inside the identity-mapped executable image range.

The kernel runs at EL1h (`PSTATE.SP = 1`, `SP_EL1` selected). On exception
entry the PE selects the target Exception level's stack pointer and sets
`PSTATE.SP = 1`, so a synchronous exception from the kernel enters the
current-EL-with-`SP_ELx` synchronous slot at offset `0x200`. The implementation
provides that slot plus deterministic fatal stubs for the other 15 slots.

Offsets implemented (all 16 symbols are exported and checked by the smoke
runner):

| Offset | Slot |
| --- | --- |
| `0x000` | current EL with `SP_EL0`, synchronous |
| `0x080` | current EL with `SP_EL0`, IRQ |
| `0x100` | current EL with `SP_EL0`, FIQ |
| `0x180` | current EL with `SP_EL0`, SError |
| `0x200` | current EL with `SP_ELx`, synchronous (implemented) |
| `0x280` | current EL with `SP_ELx`, IRQ |
| `0x300` | current EL with `SP_ELx`, FIQ |
| `0x380` | current EL with `SP_ELx`, SError |
| `0x400` | lower EL using AArch64, synchronous |
| `0x480` | lower EL using AArch64, IRQ |
| `0x500` | lower EL using AArch64, FIQ |
| `0x580` | lower EL using AArch64, SError |
| `0x600` | lower EL using AArch32, synchronous |
| `0x680` | lower EL using AArch32, IRQ |
| `0x700` | lower EL using AArch32, FIQ |
| `0x780` | lower EL using AArch32, SError |

### Fixed exception frame

Every slot saves the same fixed frame on `SP_EL1` before calling the C++
reporter. The frame size is 288 bytes (`0x120`), a multiple of 16, so the
call keeps the AAPCS64 stack alignment. `SP_EL1` itself is not otherwise
modified: the frame is built below the interrupted stack.

| Offset | Contents |
| --- | --- |
| `0x000`-`0x0f0` | `x0`-`x30`, two registers per 16-byte pair |
| `0x0f8` | `ELR_EL1` |
| `0x100` | `SPSR_EL1` |
| `0x108` | `ESR_EL1` |
| `0x110` | `FAR_EL1` |
| `0x118` | reserved/padding |
| `0x120` | frame size |

The PE preserves `ELR_EL1`, `SPSR_EL1`, and (for synchronous exceptions)
`ESR_EL1`; software must preserve the general-purpose registers. The C++
reporter receives a pointer to this frame and the vector index.

### BRK64 diagnostics and FAR_EL1

After the MMU is enabled and the PL011 read/write marker is printed, the stage
installs `VBAR_EL1` and executes `brk #0x1234` from a labelled probe. The
Breakpoint Instruction exception is synchronous. For an A64 `BRK`:

- `ESR_EL1.EC = 0x3C`.
- `ESR_EL1.IL = 1`.
- `ESR_EL1.ISS[15:0]` is the zero-extended 16-bit immediate (`0x1234`).
- `ELR_EL1` is the address of the `BRK` instruction, not the following
  instruction.

The reporter prints the vector name, `ESR_EL1` with decoded EC/IL/ISS,
`ELR_EL1`, `FAR_EL1`, and `SPSR_EL1`, then halts through PSCI `SYSTEM_OFF`.
It uses the platform-owned PL011 base; the FDT parser accepts a console
candidate only when its base equals that trusted value, so the early and fatal
paths always target the same UART. The runner asserts the vector slot, the
`EXCEPTION SYNC` marker, the exact BRK64 syndrome `0xF2001234` (`EC=0x3C`,
`IL=1`, `ISS=0x1234`), the BRK immediate, `ELR_EL1` equal to the probe symbol,
and the clean `EXCEPTION HALT` marker.

`FAR_EL1` is architecturally updated only for Instruction Abort, Data Abort,
PC alignment fault, and Watchpoint exceptions. For all other exceptions,
including `BRK`, `FAR_EL1` is UNKNOWN. The stage therefore seeds `FAR_EL1`
with `0x5a5a5a5a5a5a5a5a` immediately before the probe. The reporter must
print that exact value, which proves the frame saves and reports `FAR_EL1`
without claiming a fault address that the architecture does not provide.

### No exception return

The reporter is `[[noreturn]]` and calls the existing PSCI `SYSTEM_OFF`. No
`ERET` is executed. `ERET` restores `PSTATE` from `SPSR_EL1` and branches to
`ELR_EL1`; recovery would additionally require legal-return validation and a
real handler policy. Both are out of scope for this fatal diagnostic stage.
The other 15 vector slots use the same fixed frame and reporter with their
vector index and therefore halt deterministically rather than falling through
or returning.

### Specification provenance (exceptions)

The implementation was written against the complete Arm DDI 0487 M.c sections
and the Arm Learn the Architecture exception-model guide, not summaries:

- D1.2.2.1 stack pointer register selection (RCNNQX, ICSFMY: the target
  Exception level's stack pointer is selected on exception entry).
- D1.4.1.5 preferred exception return address (RQYCWH: a synchronous exception
  other than an exception-generating instruction returns to the generating
  instruction; RDPLYN writes it to `ELR_ELx`).
- D1.4.1.6 exception vectors (RBRCKV `VBAR_ELx`, RPJKRC fixed offsets,
  IYCTHH/IQLHPV vector categorization, RRYXCL offset tables including
  `0x000`/`0x200` synchronous and `0x180`/`0x380` SError).
- D1.4.2 exception entry (RDQXFW: `SPSR_ELx`, `ELR_ELx`, `ESR_ELx`, and the
  vector branch; RWTXBY: `PSTATE.SP = 1`).
- D1.4.2.1 synchronous exception entry (RZTSSH: `FAR_ELx` is captured only for
  Instruction Abort, Data Abort, PC alignment, and Watchpoint; for all other
  exceptions `FAR_ELx` is UNKNOWN).
- D1.4.4.1 legal exception returns and D1.4.4.2 illegal exception returns;
  C6.2.157 `ERET` restores `PSTATE` from `SPSR_ELx` and branches to
  `ELR_ELx`.
- D2.1.3 Breakpoint Instruction exceptions; D2.7.3.1 table D2-7 (`EC=0x3C`,
  `IL=1`, `ISS[15:0]` = immediate, `ISS[24:16]` RES0); D2.7.3.2 preferred
  return address is the address of the breakpoint instruction.
- D24.2.44 `ESR_EL1` (`EC[31:26]`, `IL[25]`, `ISS[24:0]`); D24.2.47
  `FAR_EL1`; D24.2.213 `VBAR_EL1` (`VBA[63:11]`, bits `[10:0]` RES0);
  C5.2.5 `ELR_EL1`; C5.2.21 `SPSR_EL1`.
- Arm Learn the Architecture, *Exception model* (ARM062-1010708621-27,
  version 1.0): section 5.2 "Taking an exception" (PE preserves `ELR`/`SPSR`,
  software preserves general-purpose registers), section 5.5 "Returning from
  an exception" (`ERET` restores `PSTATE` from `SPSR` and PC from `ELR`),
  section 5.6 "Exception stacks" (the target Exception level's `SP_ELx` is
  selected), and section 6 "The vector tables" (vector base and fixed
  offsets).
  <https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Exception%20model.pdf>

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

# Focused AArch64 exception frame/vector layout test (host)
cmake --build kernel/build-tests --target astia_aarch64_exception_frame_test
ctest --test-dir kernel/build-tests -R '^astia_aarch64_exception_frame_test$' \
  --output-on-failure

# x86 page-map regression (device is a documented no-op)
cmake --build kernel/build-tests --target astia_page_map_test
ctest --test-dir kernel/build-tests -R '^astia_page_map_test$' \
  --output-on-failure

# Production AArch64 early boot + MMU smoke
kernel/scripts/run-aarch64-virt-smoke.sh
```

The smoke runner also asserts the post-MMU marker, the exact MAIR/TCR values,
`SCTLR_EL1.M/C/I`, a PL011 write followed by a PL011 read, the 2 KiB-aligned
vector-table base and all 16 slot offsets, the BRK64 `ESR_EL1` syndrome,
`ELR_EL1`, the seeded `FAR_EL1`, and the clean exception halt.

Kernel CI (`.github/workflows/kernel-ci.yml`) runs the focused host parser
test, the production AArch64 QEMU smoke, and the existing x86 build/test
pipeline. The full host CTest suite includes the exception frame/vector layout
test.

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
- The exception path is fatal diagnostics only. It has no recovery, no
  `ERET`, no IRQ/FIQ/SError dispatch, and no user fault handling; the 15
  non-synchronous-current-EL slots are deterministic fatal stubs.
- `FAR_EL1` is architecturally UNKNOWN for BRK64; the probe seeds it so the
  runner can verify frame capture, not a fault address.
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
