# AArch64 QEMU virt early boot stage

This document describes the production AArch64 `virt` early boot target added
for `ARCH=aarch64 PLATFORM=virt BOOT=virt`. It is deliberately a **stage**, not
a full kernel boot. The generic `kernel/src/kmain.cpp` graph requires a full
driver model, a full generic IRQ subsystem, timers, and the scheduler, none of
which exist for AArch64 in this milestone. The stage now owns a bounded GICv3
boot-CPU self-SGI path, a fixed local SGI/PPI dispatch table, a bounded
five-tick EL1 physical-timer PPI 30 proof, and a fixed-capacity IRQ-context
deadline queue, but it does not expose an SPI/device IRQ subsystem, a
scheduler tick service, thread sleep/wake, or an unbounded timer queue.

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
10. Map the validated GICv3 distributor and redistributor windows as
    Device-nGnRE and initialize the boot CPU's GICv3 path: distributor
    `ARE_NS`/`EnableGrp1NS`, matching redistributor wake, SGI/PPI Group-1
    defaults, and the EL1 CPU interface.
11. With IRQs still masked, register the self-SGI and timer PPI handlers in
    the fixed 32-entry boot-CPU local dispatch table, then print the
    registration marker.
12. Trigger one reserved self-SGI through `ICC_SGI1R_EL1`, take the IRQ
    through the current-EL `SP_ELx` IRQ slot, acknowledge it with
    `ICC_IAR1_EL1`, complete it with `ICC_EOIR1_EL1` plus `ICC_DIR_EL1`
    (`EOImode == 1`), return with `ERET`, and verify the handler ran exactly
    once.
13. With IRQs masked, schedule three bounded deadlines (A at +10 ms, B at
    +30 ms, C at +20 ms), cancel C, and arm one bounded EL1 physical-timer
    one-shot on PPI 30 and re-arm it inside the registered handler for a fixed
    five-tick budget. Each timer handler dispatches due deadlines before
    `EOIR`/`DIR`; each re-arm also happens before `EOIR`/`DIR` so the
    level-sensitive source is deasserted. Unmask IRQs, wait with a bounded
    spin, mask IRQs, and verify exactly five timer interrupts were
    acknowledged, handled, completed, and returned from, with deadline A and
    B each fired once in that order, C canceled, and no pending deadlines.
14. Execute a deliberate `brk #0x1234`, report the synchronous BRK64
    exception through the C++ fatal reporter, and halt through PSCI
    `SYSTEM_OFF`.

Explicit non-goals: scheduler, full virtual-memory management, user address
spaces, cache maintenance policy, a full generic IRQ subsystem, SPI or device
IRQ dispatch, a generic timer abstraction, a scheduler tick service or timer
queue, other PPI/SPI drivers, external device interrupts, modules, userspace,
SMP, secondary redistributors, PSCI CPU bring-up, initrd, ACPI, and full
generic-kernel boot.

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
- the GICv3 distributor and redistributor windows from the node whose
  `compatible` list contains exactly `arm,gic-v3` and which carries the
  `interrupt-controller` property

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
- GICv3 nodes require `interrupt-controller`, `#interrupt-cells = <3>`,
  `#redistributor-regions = <1>` (the property is optional and defaults to
  one), exactly two `reg` pairs, non-zero 4 KiB-aligned windows, no
  base+size overflow, and no distributor/redistributor overlap
- a node whose compatible list contains a known legacy GIC string
  (`arm,cortex-a15-gic`, `arm,gic-400`, or `arm,gic-v2`) and the
  `interrupt-controller` property is rejected as `bad-gic`
- a second GICv3 node is rejected; a GICv3-ITS child (`arm,gic-v3-its`) is
  ignored because it is not an `interrupt-controller` and does not match the
  exact `arm,gic-v3` compatible string
- the GIC node is located by its properties, not by a hard-coded node path or
  unit address, so property order does not matter

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
| GICD MMIO | 0 (Device-nGnRE) | 00 | 00 (privileged RW) | 1 | 1 |
| GICR MMIO | 0 (Device-nGnRE) | 00 | 00 (privileged RW) | 1 | 1 |

`page_flags::device` selects Attr0 and forces PXN=UXN=1 in the backend.
`page_flags::no_execute` sets both PXN and UXN for Normal mappings.
`page_flags::user` sets AP[1] and nG; `writable` selects AP[2]=0. The shared API
has no executable-positive flag, so “device is non-executable” is backend
policy rather than a rejected flag combination. x86_64 deliberately treats
`page_flags::device` as a no-op; its mappings and ABI are unchanged.

RAM is mapped first with 2MiB Normal blocks (NX), then the DTB, image, and
stack ranges are installed with 4KiB leaves; the image leaves are executable
because text and data currently share pages. The PL011, GICD, and GICR windows
are 4KiB Device-nGnRE leaves. Identity is required for the current PC, SP,
page tables, DTB, PL011, and GIC across the enable transition.

The GIC windows come from the validated DTB node, not from a platform
fallback. Before mapping, the stage rejects a missing GIC, a window larger
than the platform sanity caps (`1 MiB` for GICD, `16 MiB` for GICR), a window
outside the 40-bit PA policy, a window overlapping usable RAM, the image, the
DTB, the stack, the PL011, or the other GIC window. This keeps a hostile DTB
from turning RAM into Device memory or overlapping a trusted boot range. The
QEMU `virt` GICv3 windows are GICD `0x0800_0000`/`0x1_0000` and GICR
`0x080A_0000`/`0xF6_0000`; the 16 MiB cap covers 128 redistributor frames at
the GICv3 `0x2_0000` stride.

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

This section describes the synchronous exception path. It is a fatal
diagnostic path only: there is no synchronous-exception return or recovery,
and FIQ/SError handling remains fatal. The current-EL `SP_ELx` IRQ slot is
the separate returnable path described under "GICv3 boot-CPU self-SGI".

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

### No synchronous-exception return

The synchronous reporter is `[[noreturn]]` and calls the existing PSCI
`SYSTEM_OFF`. No `ERET` is executed on that path. `ERET` restores `PSTATE`
from `SPSR_EL1` and branches to `ELR_EL1`; recovery would additionally require
legal-return validation and a real handler policy. Both are out of scope for
the fatal synchronous diagnostic path. The 14 vector slots other than the
current-EL `SP_ELx` synchronous and IRQ slots use the same fixed frame and
fatal reporter with their vector index and therefore halt deterministically
rather than falling through or returning.

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

## GICv3 boot-CPU self-SGI and timer PPI

This milestone adds a single, bounded GICv3 path for the boot CPU. It is not a
generic interrupt subsystem: only the current-EL `SP_ELx` IRQ slot is
returnable, only the reserved SGI 15 and the EL1 physical-timer PPI 30 INTIDs
are accepted, and every other vector or INTID remains fatal.

### Security-state and platform assumptions

The stage runs at Non-secure EL1. It requires a single-security-state GIC
(`GICD_CTLR.DS == 1`), which is what QEMU `virt,secure=off` provides. The
initial `GICD_CTLR` read must have `DS == 1`; otherwise initialization fails
with `not-single-security-state` instead of writing a two-security-state
Distributor by accident. In a two-security-state system, `GICR_WAKER` is a
Secure register and is RAZ/WI to Non-secure accesses (IHI 0069G §12.11.40);
the wake poll would time out deterministically rather than silently
continuing. The QEMU runner therefore uses
`-machine virt,secure=off,virtualization=off,gic-version=3`. Without
`gic-version=3`, QEMU's `virt` machine emits a GICv2 DTB
(`arm,cortex-a15-gic`), which this parser rejects as `bad-gic`.

### Distributor: ARE_NS, Group 1, and RWP

The Distributor control sequence is:

1. Read `GICD_CTLR`; require `DS == 1` (single Security state).
2. Clear `EnableGrp0` (bit 0) and `EnableGrp1NS` (bit 1), write back.
3. `DSB SY`, then poll `GICD_CTLR.RWP` (bit 31) until it reads 0.
4. Read `GICD_CTLR`, require both Group enables to read 0.
5. Set `ARE_NS` (bit 4), write back.
6. `DSB SY`, then poll `RWP` until 0.
7. Read `GICD_CTLR`, require `ARE_NS == 1`.
8. Set `EnableGrp1NS`, write back.
9. `DSB SY`, then poll `RWP` until 0.
10. Read `GICD_CTLR`, require `EnableGrp1NS == 1`.

With `DS == 1`, bits 0 and 1 are the Group 0 and Group 1 enables; both are
disabled before `ARE_NS` is changed. Clearing the Group enables first follows
the architectural constraint that changing `ARE_NS` from 0 to 1 is
UNPREDICTABLE unless the Group 1 Non-secure enable is 0 (IHI 0069G §12.9.4);
this implementation is stricter and disables both groups before touching
affinity routing. The QEMU value printed by the runner is `0x0000_0052`:
`DS == 1`, `ARE_NS == 1`, `EnableGrp1NS == 1`, `EnableGrp0 == 0`. The code
does not infer `DS` from a two-security-state layout; it reads the bit and
fails with `not-single-security-state` when it is clear.

`RWP` is read-only and indicates that a previous write to the Group enables,
the ARE bits, or `GICD_ICENABLER<n>` is still propagating (IHI 0069G
§12.9.4). The stage waits after each of the three `GICD_CTLR` writes and fails
with `distributor-rwp-timeout` rather than proceeding with a partially visible
configuration. `DSB SY` is used because the GIC MMIO pages are mapped as
Device-nGnRE, which is non-shareable; a system-wide `DSB` is the conservative
completion barrier.

### Matching and waking the boot redistributor

The GICR window is scanned in `0x2_0000`-byte GICv3 redistributor frames. For
each frame, `GICR_TYPER[63:32]` is compared with the boot PE's affinity built
from `MPIDR_EL1` (`Aff3.Aff2.Aff1.Aff0`). Scanning stops after the frame whose
`GICR_TYPER.Last` bit (bit 4) is set. If no frame matches, the stage fails
with `no-redistributor`; it never assumes frame zero.

The matching frame is woken by clearing `GICR_WAKER.ProcessorSleep` (bit 1),
executing `DSB SY`, and polling `GICR_WAKER.ChildrenAsleep` (bit 2) until it
reads 0, as required by the GICv3 software overview §5.2.1 and IHI 0069G
§12.11.40. The poll is bounded and fails with
`redistributor-wake-timeout`. This wake happens before the CPU interface is
configured, which the software overview requires to avoid UNPREDICTABLE
behavior.

### SGI/PPI Group-1 defaults

For the matching redistributor's SGI frame (`GICR` base + `0x1_0000`):

- `GICR_IGROUPR0` is written `0xFFFF_FFFF`, assigning SGIs 0-15 and PPIs 16-31
  to Non-secure Group 1.
- `GICR_IPRIORITYR0..7` are written `0xA0A0A0A0`, giving every SGI/PPI the
  same mid-range priority. `ICC_PMR_EL1` is later set to `0xF0`, so the
  self-SGI and timer PPI are not masked.
- `GICR_ICFGR1` is written 0, leaving PPIs level-sensitive. `GICR_ICFGR0`
  (SGIs) is not programmed because SGIs are always edge-triggered and the
  register is RAO/WI for them (DAI 0492 §5.2.4).
- `GICR_ISENABLER0` enables the reserved self-SGI bit 15 and the EL1
  physical-timer PPI bit 30. The stage reads the set-enable register back and
  requires both bits, failing with `sgi-not-enabled` or
  `timer-ppi-not-enabled` otherwise.

The reserved self-SGI INTID is 15 (`gicv3::self_sgi_id`); the EL1 physical
timer PPI is INTID 30 (`timer::ppi_id`). No other SGI/PPI is enabled.

### CPU interface

The CPU interface is enabled in this order:

| Register | Value | Verification |
| --- | --- | --- |
| `ICC_SRE_EL1` | `SRE = 1`, `DIB = 1`, `DFB = 1`, other bits preserved | read back `SRE == 1`, `DIB == 1`, `DFB == 1` |
| `ICC_PMR_EL1` | `0xF0` | read back and print |
| `ICC_BPR1_EL1` | `0` | read back and print |
| `ICC_CTLR_EL1` | `EOImode = 1`, other bits preserved | read back `EOImode == 1` |
| `ICC_IGRPEN1_EL1` | `Enable = 1` | read back `Enable == 1` |

`ICC_SRE_EL1.SRE` must be 1 before the other `ICC_*` system registers are
usable (IHI 0069G §12.1.7, §12.2.22). `DIB` disables IRQ bypass and `DFB`
disables FIQ bypass (IHI 0069G §3.2, §12.2.22). Disabling IRQ bypass is
required before enabling Group 1 signaling: the architecture states that
`ICC_IGRPEN1_EL1.Enable` must be 0 when `ICC_SRE_ELx.DIB == 0`, otherwise the
behavior is UNPREDICTABLE. The stage therefore sets `SRE`, `DIB`, and `DFB`,
then reads the register back and requires all three before it programs
`ICC_PMR_EL1`, `ICC_BPR1_EL1`, `ICC_CTLR_EL1`, or `ICC_IGRPEN1_EL1`. The
QEMU `secure=off` single-security-state/no-EL2 configuration makes these
fields writable at EL1; a system where they are read-only aliases would fail
deterministically with `sre-bypass-not-disabled`.

`ICC_PMR_EL1` sets the priority mask; `0xF0` allows the priority-`0xA0`
self-SGI. `ICC_BPR1_EL1` is written 0; QEMU implements five priority bits and
reads back the architectural minimum `3`, so the runner asserts the observed
`3` while the code's requested value remains 0. `ICC_CTLR_EL1.EOImode = 1`
separates priority drop from deactivation (DAI 0492 §6.4, IHI 0069G
§12.2.6). `ICC_IGRPEN1_EL1` enables Group 1 signaling for the current
Security state (DAI 0492 §5.2.2, IHI 0069G §12.2.16).

### Required ordering and barriers

The GIC architecture makes system-register writes visible to the Redistributor
only under explicit ordering (IHI 0069G §12.1.6):

- `ICC_PMR_EL1` writes are self-synchronizing.
- A `DSB` guarantees that the last `ICC_SGI1R_EL1`, `ICC_IGRPEN1_EL1`, and
  `ICC_EOIR1_EL1`/`ICC_DIR_EL1` writes are observed by the associated
  Redistributor.
- An `ISB` must precede the `DSB` so the system-register writes are visible
  to it.

The implementation therefore uses:

- `ISB` after `ICC_SRE_EL1`, `ICC_PMR_EL1`, `ICC_BPR1_EL1`, and
  `ICC_CTLR_EL1` writes.
- `ISB; DSB SY; ISB` after enabling `ICC_IGRPEN1_EL1`.
- `ISB; DSB SY; ISB` after writing `ICC_SGI1R_EL1`.
- `ISB; DSB SY; ISB` after `ICC_EOIR1_EL1` and `ICC_DIR_EL1`.
- `DSB SY` after `GICD_CTLR` writes, after the redistributor configuration
  writes, and after clearing `GICR_WAKER.ProcessorSleep`, before reading back
  or polling.

The GIC MMIO pages are Device-nGnRE, which is non-shareable. A system-wide
`DSB SY` is therefore the conservative completion barrier for the
Distributor, Redistributor, and wake sequencing. `ISB` is retained only where
the architecture requires context synchronization for system-register writes
(`ICC_SRE_EL1`, `ICC_PMR_EL1`, `ICC_BPR1_EL1`, `ICC_CTLR_EL1`,
`ICC_IGRPEN1_EL1`, `ICC_SGI1R_EL1`, `ICC_EOIR1_EL1`, `ICC_DIR_EL1`).

### Self-SGI encoding

`ICC_SGI1R_EL1` is encoded for the boot PE with `IRM = 0` (target by
affinity, not "all except self"):

- `TargetList[15:0]` has the bit for the PE's `Aff0` low nibble.
- `RS[47:44]` selects the 16-`Aff0` group for `Aff0 >= 16`.
- `Aff1[23:16]`, `Aff2[39:32]`, and `Aff3[55:48]` carry the PE's affinity.
- `INTID[27:24]` is the reserved SGI ID 15.

This follows DAI 0492 §7.1.2 and IHI 0069G §12.2.21. The QEMU boot PE has
`MPIDR_EL1 = 0x8000_0000`, so the encoded value is
`0x0F00_0001` (INTID 15, target list bit 0). The handler prints the actual
`ICC_IAR1_EL1` value to prove the ack.

### Current-EL `SP_ELx` IRQ entry and return

The vector table keeps the architectural 16-slot layout. Only the
current-EL-with-`SP_ELx` IRQ slot at `0x280` is changed: it branches to an
out-of-line trampoline because the full save/call/restore sequence does not
fit in the 128-byte slot. The trampoline:

1. Saves the same fixed 288-byte `exception_frame` used by the fatal paths.
2. Calls `astia_aarch64_irq_entry(frame)`.
3. Restores `x0`-`x30`, `ELR_EL1`, and `SPSR_EL1`, adjusts `SP_EL1`, and
   executes `ERET`.

The IRQ handler reads `ICC_IAR1_EL1` once and preserves the complete value for
the `ICC_EOIR1_EL1` priority drop. It dispatches the registered local handler
for the acknowledged INTID before writing `ICC_EOIR1_EL1` (priority drop)
followed by `ICC_DIR_EL1` (deactivation). For PPI 30 the timer handler either
re-arms the next bounded positive TVAL or, on the final tick, disables and
masks the timer and moves the compare point to a bounded positive value. Both
paths run before EOIR/DIR, so the level-sensitive source is deasserted and
does not re-fire. INTID 1023 is the architectural spurious value and returns
without EOI; every unregistered local INTID, SPI, extended PPI, and special
INTID 1020-1022 is fatal and halts through PSCI `SYSTEM_OFF`. All other vector
slots remain the existing deterministic fatal stubs.

The main path unmasks `PSTATE.I` with `MSR DAIFClr, #2`, triggers the SGI,
busy-waits with a bounded `YIELD` loop for the handler counter, then masks
`PSTATE.I` again with `MSR DAIFSet, #2`. It does not use `WFI`: a lost-wakeup
between the counter check and `WFI` would hang the boot CPU. The bounded loop
turns a missing interrupt into a printed failure.

### Boot-CPU local IRQ registration and dispatch

Before `IRQ ENABLED`, the stage registers exactly two handlers in a fixed
32-entry local table: reserved SGI 15 and timer PPI 30. `register_local_irq`
takes a function pointer plus an opaque context, rejects INTIDs outside 0-31,
null handlers, and duplicate INTIDs, and performs no allocation, locking, or
scheduler interaction. Registration is only valid on the boot CPU while
`PSTATE.I` is set; there is no unregister or runtime mutation path.

`acknowledge_and_complete()` reads `ICC_IAR1_EL1` once, classifies the INTID,
and invokes the registered handler before any completion write. It then writes
the complete IAR value to `ICC_EOIR1_EL1` for the priority drop and the INTID
to `ICC_DIR_EL1` for deactivation (`EOImode == 1`). A handler for a
level-sensitive source must make that source inactive before returning,
because the priority drop happens before deactivation and an asserted level
would otherwise leave the interrupt active-and-pending. The timer handler
therefore re-arms a positive interval, or disables and masks `CNTP_CTL_EL0`
and moves the compare point on the final tick, before returning; the SGI
handler only counts the event.

INTIDs 0-15 are SGIs, 16-31 are PPIs, 32-1019 are SPIs, and 1020-1023 are
special (IHI 0069G Table 2-1, §2.2). Only a registered INTID in 0-31 is
handled. An unregistered local INTID, every SPI, extended PPI, or special
INTID 1020-1022 is fatal. INTID 1023 is the architectural spurious value and
returns without EOI.

Non-goals for this foundation: SPIs and external devices, INTIDs above 31,
secondary CPUs and per-CPU tables, dynamic unregister, interrupt priorities
beyond the fixed boot policy, nested/preempted dispatch, scheduler wakeups,
and any generic IRQ abstraction.

### EL1 physical timer PPI 30

The stage now proves one bounded, five-tick EL1 physical-timer interrupt
sequence with the re-arm inside the registered handler, plus a fixed-capacity
deadline queue dispatched from that handler. It is not a timer subsystem or a
scheduler tick source: there is no unbounded timer queue, scheduler
interaction, generic timer abstraction, or EL0 access.

The interrupt contract is:

- The source is the EL1 physical timer output, wired to PPI INTID 30
  (`timer::ppi_id`), the Arm SBSA recommended Non-secure EL1 timer PPI.
- The redistributor assigns PPI 30 to Non-secure Group 1
  (`GICR_IGROUPR0`), priority `0xA0` (`GICR_IPRIORITYR`), and level-sensitive
  behavior (`GICR_ICFGR1 = 0`). `ICC_PMR_EL1 = 0xF0` admits priority `0xA0`.
- The timer condition is
  `((PhysicalCountInt() - Offset) - CompareValue) >= 0`. With `ENABLE == 1`
  and `IMASK == 0`, the output is asserted while the condition is met and
  `ISTATUS` is 1; `ISTATUS` ignores `IMASK` (DDI 0487 M.d §D12.2.4.1,
  §D24.10.22).
- Arming uses `CNTP_TVAL_EL0` rather than `CNTP_CVAL_EL0`. A TVAL write sets
  `CNTP_CVAL_EL0 = CNTPCT_EL0 + SignExtend(TimerValue[31:0])`, so the policy
  accepts only a positive interval below `0x8000_0000` (DDI 0487 M.d
  §D12.2.4.2, §D24.10.24). A zero frequency or unrepresentable interval fails
  before any sysreg write. `timer::arm_interval` rejects `ticks == 0` and
  `ticks > timer::tval_max_positive` with `interval-unrepresentable` before
  any sysreg write; the one-shot path derives ticks and delegates to it.
- The fixed interval is 10 ms (`timer::one_shot_interval_ms`). Ticks are
  `ceil(frequency_hz * 10 / 1000)` with overflow and `INT32_MAX` checks. On
  QEMU `cortex-a57`, `CNTFRQ_EL0 = 0x03B9_ACA0` (62.5 MHz) and the interval is
  625,000 ticks (`0x0009_8968`).
- Arming writes `CNTP_CTL_EL0` disabled and masked, writes `CNTP_TVAL_EL0`,
  then writes `ENABLE = 1, IMASK = 0`, with `ISB` after each timer system
  register write. The read-back control line records `ENABLE`, `IMASK`, and
  the observed `ISTATUS`.
- On acknowledge, the handler counts the tick and asks
  `timer::next_tick_action(handled_before, timer::periodic_tick_budget)`, where
  `handled_before` is the count before the current tick and the budget is five
  (`timer::periodic_tick_budget`). For `handled_before` 0-3 it re-arms the next
  10 ms interval with `timer::arm_interval`; for `handled_before` 4 it returns
  `tick_action::stop` and the handler disables and masks the timer and moves
  the compare point to the maximum positive TVAL. A zero budget or
  `handled_before >= budget` also stops, and the last permitted tick stops
  rather than re-arming. Both branches run inside the handler before any
  priority drop. This deasserts the level source, so `EOIR`/`DIR` cannot leave
  the PPI active and pending. The timer is not masked or disabled after EOI.
  A rejected re-arm disables the timer and records a printed failure instead of
  retrying.
- `ICC_CTLR_EL1.EOImode == 1` splits the completion: the write to
  `ICC_EOIR1_EL1` drops priority and the write to `ICC_DIR_EL1` deactivates
  the interrupt. The entire `ICC_IAR1_EL1` value is preserved for the EOIR
  write; the INTID is used for DIR. This follows the GIC lifecycle,
  activation, priority-drop, and deactivation rules and the requirement not to
  use a partial IAR value (IHI 0069G §4.1.1).
- The GIC MMIO ordering is unchanged: `DSB SY` after redistributor writes,
  `ISB; DSB SY; ISB` around `ICC_EOIR1_EL1`/`ICC_DIR_EL1`, and `ISB` before
  those drains. The timer sysreg writes add `ISB` but do not weaken the
  existing GIC barriers.

The boot path proves this sequence: after the self-SGI proof and with IRQs
masked, it prints the PPI, frequency/interval, and
`timer: tick-budget=0x00000005` diagnostics, arms the one-shot, prints
`TIMER ARMED`, unmasks IRQs, waits with a bounded `YIELD` loop until the timer
counter reaches the budget, masks IRQs, and requires exactly five handled
timer interrupts with no failed re-arm before printing `TIMER RETURNED` and
`TIMER TICK OK`. The deliberate BRK64 fatal proof follows immediately. A
missing, failed, or extra timer interrupt is a printed failure, not a hang.
The smoke runner asserts the five per-tick `IAR`/`EOIR`/`DIR`/`HANDLED` blocks
with a state machine, and the interval bounds are covered on the host by the
pure `timer::valid_interval` predicate.

### Fixed-capacity IRQ-context deadline queue

`timer.hpp` adds a bounded deadline table to the same PPI 30 path. It is a
deadline policy inside the timer handler, not a general timer subsystem.

- Capacity is eight entries (`timer::deadline_capacity`). Storage is an
  `inline` static array (`timer::deadline_table`); scheduling performs no
  allocation and the table never grows.
- `schedule_deadline(now_ms, delay_ms, handler, context, id)` computes an
  absolute `due_ms` with overflow-checked addition. It rejects
  `delay_ms == 0` and an unrepresentable absolute deadline with
  `invalid_delay`, a null handler with `null_handler`, and a full table with
  `full`. Success returns `none`, arms the first free slot, and returns a
  non-zero id. Ids increase monotonically, skip zero on wrap, and are not
  reused until `reset_deadlines`.
- `cancel_deadline(id)` clears the matching armed slot and returns true; an
  unknown or already-fired id returns false. `pending_deadlines()` counts
  armed slots and `reset_deadlines()` clears the table and restarts the id
  sequence at one.
- `dispatch_due(now_ms)` is earliest-deadline-first within the fixed table. It
  repeatedly selects the armed entry with the smallest `due_ms`, breaking ties
  by the lowest slot index, and fires every entry whose `due_ms <= now_ms`.
  This is a catch-up policy: one call can fire several overdue entries, in
  non-decreasing due order, and an arming never fires twice. A slot is cleared
  before its callback runs, so a callback may schedule or cancel
  re-entrantly; a newly scheduled future deadline does not fire in the same
  pass.
- Deadline callbacks run in IRQ context inside the registered PPI 30 timer
  handler, before `ICC_EOIR1_EL1`/`ICC_DIR_EL1`. They must not block or enable
  interrupts, and they must return promptly so the level-sensitive source is
  deasserted.
- `now_ms()` reads `CNTPCT_EL0` and `CNTFRQ_EL0` and converts with
  `count_to_ms`, which rejects a zero frequency and overflow-checked
  `count * 1000`; it returns zero if the conversion is unrepresentable. Reads
  need no `ISB`, and the function performs no system-register writes.
- The host test covers capacity, conversion and overflow, due boundaries,
  tie-breaking, scheduling errors, cancellation, catch-up ordering, and
  reset. The boot proof schedules A (+10 ms), B (+30 ms), and C (+20 ms),
  cancels C, and requires A then B to fire exactly once each across the
  five-tick window with no pending deadlines.

Explicit timer non-goals: repeating timers beyond the fixed five-tick proof,
`CNTV*` virtual timers, EL2/EL3 timers, an unbounded timer queue or timer
wheel, scheduler handoff, thread sleep/wake, deadline priorities or priority
inheritance, SMP, broadcast timers, CPU hotplug/PSCI timer state, `WFI`-based
idle, and SPI registration. The local table is intentionally SGI/PPI-only.

### Specification provenance (GICv3/IRQ)

The implementation was written against the complete documents, not
summaries:

- Arm, *GICv3 and GICv4 Software Overview*, DAI 0492 version 3.0,
  <https://developer.arm.com/documentation/dai0492>:
  §4.5.2 Redistributors; §4.5.3 CPU interfaces; §5.1 Global settings;
  §5.2.1 Redistributor configuration (`GICR_WAKER`); §5.2.2 CPU interface
  configuration (`ICC_SRE_ELn`, `ICC_PMR_EL1`, `ICC_BPRn_EL1`,
  `ICC_CTLR_ELn.EOImode`, `ICC_IGRPEN1_EL1`); §5.2.3 PE configuration
  (`PSTATE` masks, `VBAR_ELn`); §5.2.4 SPI/PPI/SGI configuration; §6.2 Taking
  an interrupt (`ICC_IAR0/1_EL1`, special INTIDs 1020-1023); §6.4 End of
  interrupt (`EOImode`); §7.1 Generating SGIs; §7.1.1 SGI ID; §7.1.2
  `IRM`/TargetList; §7.1.3 SGI grouping.
- Arm, *Generic Interrupt Controller Architecture Specification, GIC
  architecture version 3 and version 4*, IHI 0069G (ID011821),
  <https://developer.arm.com/documentation/ihi0069>:
  §2.2 and Table 2-1 (INTID partitioning); §2.2.1 Special INTIDs; §2.3
  Affinity routing; §3.2 Interrupt bypass support (`DIB`/`DFB`); §4.1
  Interrupt lifecycle; §4.4 SGIs; §4.6 Interrupt grouping; §4.7 Enabling the
  distribution of interrupts; §4.8 Interrupt prioritization; §11.1 Power
  management; §12.1.6 Observability of the
  effects of accesses to the GIC registers; §12.1.7 GIC System register
  access; §12.2.5 `ICC_BPR1_EL1`; §12.2.6 `ICC_CTLR_EL1`; §12.2.8
  `ICC_DIR_EL1`; §12.2.10 `ICC_EOIR1_EL1`; §12.2.14 `ICC_IAR1_EL1`;
  §12.2.16 `ICC_IGRPEN1_EL1`; §12.2.18 `ICC_PMR_EL1`; §12.2.21
  `ICC_SGI1R_EL1`; §12.2.22 `ICC_SRE_EL1`; §12.9.4 `GICD_CTLR`;
  §12.11.2 `GICR_CTLR`; §12.11.7 `GICR_ICFGR0`; §12.11.8 `GICR_ICFGR1`;
  §12.11.12 `GICR_IGROUPR0`; §12.11.19 `GICR_IPRIORITYR<n>`; §12.11.23
  `GICR_ISENABLER0`; §12.11.35 `GICR_TYPER`; §12.11.40 `GICR_WAKER`;
  Table 12-14 Forwarding an SGI to a target PE.
- Arm, *Arm Architecture Reference Manual for A-profile architecture*, DDI
  0487 M.d, <https://developer.arm.com/documentation/ddi0487/md>:
  §D12.1 About the Generic Timer; §D12.2.4 Timers (the timer output is a
  PPI); §D12.2.4.1 Operation of the CompareValue views; §D12.2.4.2 Operation
  of the TimerValue views; §D24.10.1 `CNTFRQ_EL0`; §D24.10.22
  `CNTP_CTL_EL0` (`ENABLE`, `IMASK`, `ISTATUS`); §D24.10.24
  `CNTP_TVAL_EL0`; and the `D24.10` Generic Timer register descriptions for
  `CNTPCT_EL0` and `CNTP_CVAL_EL0`.
- Arm, *Server Base System Architecture* (SBSA), recommended PPI INTID
  assignments: Non-secure EL1 physical timer on PPI 30.
- QEMU `virt` machine documentation and the generated DTB contract,
  <https://www.qemu.org/docs/master/system/arm/virt.html>:
  `gic-version=3` selects the GICv3 node with GICD `0x0800_0000`/`0x1_0000`,
  GICR `0x080A_0000`/`0xF6_0000`, `#redistributor-regions = <1>`,
  `compatible = "arm,gic-v3"`, and `#interrupt-cells = <3>`.

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

# Focused GICv3 SGI/timer/IRQ policy test (host)
cmake --build kernel/build-tests --target astia_aarch64_gic_test
ctest --test-dir kernel/build-tests -R '^astia_aarch64_gic_test$' \
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
vector-table base and all 16 slot offsets, the GICv3 DTB windows, the matched
redistributor affinity, `GICD_CTLR` `DS`/`ARE_NS`/`EnableGrp1NS`, the
CPU-interface `SRE`/`DIB`/`DFB`/`PMR`/`BPR1`/`EOImode`/`IGRPEN1` state, the
reserved self-SGI ID, the local registration marker and IDs, the timer PPI
30/frequency/tick-budget/arm, the deadline capacity/scheduled/canceled
diagnostics, the exactly-once `deadline: fired-a=1 fired-b=1 fired-c=0`
proof, the `DEADLINE OK` ordering between `TIMER TICK OK` and
`EXCEPTION SYNC`, and `CNTP_CTL_EL0` state, both handlers'
`IAR1`/`EOIR1`/`DIR` values, the five-tick timer-handled proof, the IRQ
returns, the BRK64 `ESR_EL1` syndrome, `ELR_EL1`, the seeded
`FAR_EL1`, and the clean exception halt. It runs QEMU with
`-machine virt,secure=off,virtualization=off,gic-version=3` so the DTB
describes GICv3 rather than QEMU's default GICv2.

Kernel CI (`.github/workflows/kernel-ci.yml`) runs the focused host parser
test, the production AArch64 QEMU smoke, and the existing x86 build/test
pipeline. The full host CTest suite includes the exception frame/vector layout
test and the GICv3 policy test.

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

- No full kernel boot, user address spaces, virtual-memory allocator, generic
  IRQ subsystem, timer subsystem, modules, userspace, SMP, initrd, or ACPI.
- The returnable exception path is limited to the current-EL `SP_ELx` IRQ
  slot, one reserved SGI, and one timer PPI. The synchronous slot is fatal
  diagnostics only, and the remaining 14 vector slots are deterministic fatal
  stubs. There is no recovery, no user fault handling, and no FIQ/SError
  dispatch.
- No external SPI, other PPI, or device driver interrupts. The only timer is
  a bounded five-tick re-arm of the 10 ms interval on PPI 30; it has no
  scheduler binding, timer queue, or virtual timer. The local dispatch table
  is a fixed boot-CPU SGI/PPI foundation, not a generic IRQ subsystem: no SPI
  dispatch, no dynamic registration, no per-CPU tables, and no scheduler
  interaction.
- No SMP, secondary redistributor wake, or PSCI CPU bring-up. The
  redistributor scan is bounded to the boot CPU's matching frame.
- The GIC path requires a single-security-state GIC (`GICD_CTLR.DS == 1`) at
  Non-secure EL1; a two-security-state system would need Secure-side
  redistributor wake.
- GICv4 VLPI/ITS and LPI support are out of scope; the GICv3-ITS child node is
  parsed only enough to ignore it.
- The GIC MMIO sanity caps are 1 MiB for GICD and 16 MiB for GICR; larger
  windows are rejected rather than mapped.
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
