# ACPI and orderly shutdown

This kernel subsystem discovers ACPI tables, validates them, interprets the AML
needed to build a namespace and evaluate `\_S5`, and enters S5 through the FADT
PM1 control registers. It also validates the FADT reset register for reboot.
The ACPI layer itself is kernel-side only; the `power(2)` syscall is the only
userspace entry point, and there is no `/dev` node, VFS file, ioctl, or raw
table/AML exposure.

## Specification basis

Implementation and review used the official **ACPI Specification, Release 6.5
(August 29, 2022)** PDF, sections:

- 4.8.1.2 PM1 Control Registers and 4.8.3.2 PM1 Control Grouping (SLP_TYPx,
  SLP_EN, PM1_CNT_LEN).
- 4.8.3.6 Reset Register, including the 8-bit register, complete-system reset,
  and the System I/O, System Memory, and bus-0 PCI Configuration address-space
  restrictions.
- 5.2.3.2 Generic Address Structure, including Address Space ID, Register Bit
  Width, Register Bit Offset, Access Size, and the bus-0 PCI Configuration
  address encoding.
- 5.2.5 RSDP, 5.2.6 System Description Table Header, 5.2.7 RSDT, 5.2.8 XSDT,
  and 5.2.9 FADT (including RESET_REG, RESET_VALUE, RESET_REG_SUP, and the
  extended X_PM1a_CNT_BLK / X_PM1b_CNT_BLK GAS fields).
- 7.4.2.6 System `\_S5` State (Soft Off), including SLP_TYPa/SLP_TYPb package
  layout.
- Chapter 20, ACPI Machine Language (AML) Specification, complete sections
  20.1-20.4: notation, table/header encoding, NameString/NameSeg encoding, data
  objects, package-length encoding, all term/named-object/statement/expression
  encodings, misc objects, byte-value table, and namespace name encoding.

The x86_64 SMP MADT path uses the same bounds-checked table reader as the ACPI
subsystem: RSDP, XSDT/RSDT, and every MADT entry are read through the mapped
identity/direct window, table lengths are capped, and malformed entries reject
the whole topology instead of producing a partial CPU inventory. AArch64
remains FDT-only. The AArch64 graph builds `kernel/src/acpi/stub.cpp`,
which supplies `astia::acpi::rsdp()` = 0 and `shutdown_supported()`/`shutdown()`
= false, so the public API links without claiming ACPI support.

## Public kernel API

`<astia/acpi.hpp>`:

- `u64 astia::acpi::rsdp()`: the validated loader-supplied RSDP physical
  address, or 0 when absent/invalid.
- `bool astia::acpi::shutdown_supported()`: FADT, DSDT, AML namespace, `\_S5`,
  and PM1 registers all validated and the platform handler registered.
- `bool astia::acpi::shutdown()`: enter the tiered shutdown sequence.
- `bool astia::acpi::reboot_supported()`: RESET_REG_SUP is set and the FADT
  reset register passed validation.
- `bool astia::acpi::reboot()`: run the ordered shutdown tiers and write
  RESET_VALUE to RESET_REG as the final action. A successful reset does not
  return; any return is a failure.

`<astia/shutdown.hpp>`:

- `tier::{prepare, flush, quiesce, platform, final}` are dispatched in ascending
  order. `final` is reserved for the single irreversible poweroff action; only
  one final handler may be registered, so no later firmware-driven work can run
  after S5. Reboot passes a one-shot reset action as the final override for that
  dispatch, skipping the registered S5 final while preserving the one-final
  registration invariant.
- `register_handler(tier, name, fn, context)` preserves registration order within
  a tier and is rejected once dispatch starts.
- `initiate()` runs each handler once. A failing handler is logged and the
  remaining handlers, including the platform tier, still run; the dispatch
  reports false if any handler failed or no platform handler exists.
- ACPI S5 registers as the `final` handler, after every subsystem `platform`
  handler.

## Validation and bounds

- The reader's firmware-table window covers every firmware-described range
  (usable, reserved, ACPI reclaimable, ACPI NVS) up to the arch direct-map
  span, so tables in reserved memory above the highest usable region are read
  through a temporary on-demand mapping instead of being rejected.
- RSDP v1/v2 signatures, both checksums, declared length, and range checks.
- RSDT/XSDT signature, checksum, length, entry alignment, duplicate/cyclic table
  addresses, table count, and maximum sizes.
- SMP MADT discovery validates the RSDP and root table bounds before walking
  entries, caps every declared length, checksums only within the mapped table,
  and rejects truncated, oversized, or out-of-range entry structures.
- A heap-backed owning table store retains a validated copy of every discovered
  SDT (root table, RSDT/XSDT entries, FADT-referenced DSDT). One allocation per
  record holds the record header and table bytes, so no firmware pointers or
  shared BSS slab are retained. Bounded count, per-table length, aggregate bytes,
  and allocation failure all clean up partial records; `find(signature, index)`
  preserves discovery order across duplicate signatures.
- FADT signature/length/checksum; DSDT from RSDT/XSDT or `Xdsdt`/`DSDT`;
  SSDTs validated and loaded in order.
- S5 entry reads each PM1a/PM1b control register, clears SLP_TYPx and SLP_EN,
  programs the `\_S5` sleep type with SLP_EN, and writes the result back.
  SCI_EN and every other non-sleep bit keep their read values, as required by
  ACPI 6.5 Table 4.13. A failed read or write aborts the transition and is
  reported and logged.
- AML fixed limits: total AML bytes, namespace nodes, values, name references,
  package items, package nesting, term/evaluation recursion depth, call depth,
  reference depth, and execution steps. Deeply nested firmware expressions are
  rejected at the term-depth limit before stack exhaustion.
- Raw table bytes and AML contents are never logged; only signature, length,
  revision, and validation outcomes.

## Supported AML subset

Namespace construction:

- `Alias`, `Name`, `Scope`, `Device`, `Method`, `External`, `Mutex`, `Event`,
  `OperationRegion`, `Field`, `IndexField`, `BankField`, `DataTableRegion`,
  `Processor`, `PowerResource`, `ThermalZone`.
- `NameString` forms: simple, dual, multi-segment, root-relative, and
  parent-prefixed, including predefined `\_SB`, `\_GPE`, `\_PR`, `\_TZ`, `\_SI`,
  `\_GL`.

Data and evaluation:

- Integers, strings, buffers, packages, variable packages, name references,
  `Revision`, locals/arguments.
- Method invocation with argument counts and bounded recursion.
- `If`/`Else`, `While`, `Return`, `Break`, `Continue`, `Noop`, `Store`,
  `CopyObject`.
- Integer arithmetic/bitwise operators, comparisons, logical operators,
  `SizeOf`, `Index`, `DerefOf`, `ObjectType`, `ToInteger`, `Notify`.
- `_S5` may be a `Name` holding a package or a zero-argument `Method` returning a
  package. The first two package elements must be integers 0-7.

## Unsupported / rejected

Unsupported constructs fail evaluation and leave normal boot running without an
S5 handler. They are not silently guessed or byte-searched:

- `CreateXField`, `RefOf`, `CondRefOf`, `Load`, `LoadTable`, `Acquire`/`Release`
  synchronization semantics, `Wait`/`Signal`/`Reset`, `Mid`, `Match`, BCD
  conversion, string formatting beyond the supported integer `ToInteger`, and
  general device/resource interpretation.
- Module-level executable code is not run; namespace definitions are parsed.
- Relative name references inside package data are resolved from the root, not
  their lexical scope. QEMU q35 `_S5` does not depend on this.
- Alias evaluation follows the target by node identity; it never copies or
  relinks namespace nodes.
- Hardware-reduced ACPI SLEEP_CONTROL_REG/SLEEP_STATUS_REG S5 is not implemented;
  only PM1a/PM1b control registers and the S5 sleep type are used.
- System suspend (S3 or another G1 sleep state), wake vectors, and resume are
  not implemented. The power syscall reports ENOTSUP rather than aliasing this
  to a process or thread sleep.
- Reboot requires a validated FADT RESET_REG. There is no triple-fault,
  keyboard-controller, or arbitrary port-write fallback. System I/O and System
  Memory reset registers are executable; bus-0 PCI Configuration is spec-valid
  but rejected as unsupported until a native byte-write contract exists, because
  the current PCI config helper performs a dword read-modify-write. Malformed
  widths, offsets, access sizes, addresses, or missing RESET_REG_SUP make
  reboot unsupported.
- No general ACPI device enumeration, `_PRT`, EC/battery/thermal, wake/resume, or
  AML debugger.

Only real firmware constructs needed for namespace construction and `_S5` are
claimed. Unsupported platforms must not assume ACPI shutdown works.

## Tests

- `acpi_host_test`: synthetic RSDP v1/v2, RSDT/XSDT, checksum/duplicate
  rejection, malformed and oversized MADT lengths, malformed MADT entries,
  duplicate APIC IDs, and out-of-range table addresses, `_S5` Name and Method,
  malformed/unsupported AML, FADT legacy and extended GAS plans, invalid SLP
  types, shutdown tier ordering/failure behavior, PM1 read-modify-write
  preservation of non-SLP control bits, PM1 read/write failure handling, and
  FADT reset plans for System I/O, System Memory, and bus-0 PCI Configuration
  space.
- `acpi_q35_shutdown`: GRUB Multiboot2 q35 boot; requires QEMU to exit normally
  with status 0 after the S5 write, plus inventory/S5/platform-handler markers.
- `acpi_q35_malformed_rsdp`, `acpi_q35_malformed_aml`: synthetic corrupt RSDP
  and malformed AML boot regressions; must reject, log, and still reach the boot
  marker.
