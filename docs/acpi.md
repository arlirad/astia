# ACPI and orderly shutdown

This kernel subsystem discovers ACPI tables, validates them, interprets the AML
needed to build a namespace and evaluate `\_S5`, and enters S5 through the FADT
PM1 control registers. It is kernel-side only: there is no `/dev` node, syscall,
VFS file, ioctl, or raw table/AML exposure.

## Specification basis

Implementation and review used the official **ACPI Specification, Release 6.5
(August 29, 2022)** PDF, sections:

- 4.8.1.2 PM1 Control Registers and 4.8.3.2 PM1 Control Grouping (SLP_TYPx,
  SLP_EN, PM1_CNT_LEN).
- 5.2.3.2 Generic Address Structure.
- 5.2.5 RSDP, 5.2.6 System Description Table Header, 5.2.7 RSDT, 5.2.8 XSDT,
  and 5.2.9 FADT (including the extended X_PM1a_CNT_BLK / X_PM1b_CNT_BLK GAS
  fields).
- 7.4.2.6 System `\_S5` State (Soft Off), including SLP_TYPa/SLP_TYPb package
  layout.
- Chapter 20, ACPI Machine Language (AML) Specification, complete sections
  20.1-20.4: notation, table/header encoding, NameString/NameSeg encoding, data
  objects, package-length encoding, all term/named-object/statement/expression
  encodings, misc objects, byte-value table, and namespace name encoding.

The existing SMP MADT parser in `kernel/src/arch/x86_64/smp.cpp` is unchanged.
AArch64 remains FDT-only. The AArch64 graph builds `kernel/src/acpi/stub.cpp`,
which supplies `astia::acpi::rsdp()` = 0 and `shutdown_supported()`/`shutdown()`
= false, so the public API links without claiming ACPI support.

## Public kernel API

`<astia/acpi.hpp>`:

- `u64 astia::acpi::rsdp()`: the validated loader-supplied RSDP physical
  address, or 0 when absent/invalid.
- `bool astia::acpi::shutdown_supported()`: FADT, DSDT, AML namespace, `\_S5`,
  and PM1 registers all validated and the platform handler registered.
- `bool astia::acpi::shutdown()`: enter the tiered shutdown sequence.

`<astia/shutdown.hpp>`:

- `tier::{prepare, flush, quiesce, platform, final}` are dispatched in ascending
  order. `final` is reserved for the single irreversible poweroff action; only
  one final handler may be registered, so no later firmware-driven work can run
  after S5.
- `register_handler(tier, name, fn, context)` preserves registration order within
  a tier and is rejected once dispatch starts.
- `initiate()` runs each handler once. A failing handler is logged and the
  remaining handlers, including the platform tier, still run; the dispatch
  reports false if any handler failed or no platform handler exists.
- ACPI S5 registers as the `final` handler, after every subsystem `platform`
  handler.

## Validation and bounds

- RSDP v1/v2 signatures, both checksums, declared length, and range checks.
- RSDT/XSDT signature, checksum, length, entry alignment, duplicate/cyclic table
  addresses, table count, and maximum sizes.
- A heap-backed owning table store retains a validated copy of every discovered
  SDT (root table, RSDT/XSDT entries, FADT-referenced DSDT). One allocation per
  record holds the record header and table bytes, so no firmware pointers or
  shared BSS slab are retained. Bounded count, per-table length, aggregate bytes,
  and allocation failure all clean up partial records; `find(signature, index)`
  preserves discovery order across duplicate signatures.
- FADT signature/length/checksum; DSDT from RSDT/XSDT or `Xdsdt`/`DSDT`;
  SSDTs validated and loaded in order.
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
- No general ACPI device enumeration, `_PRT`, EC/battery/thermal, wake/resume, or
  AML debugger.

Only real firmware constructs needed for namespace construction and `_S5` are
claimed. Unsupported platforms must not assume ACPI shutdown works.

## Tests

- `acpi_host_test`: synthetic RSDP v1/v2, RSDT/XSDT, checksum/duplicate
  rejection, `_S5` Name and Method, malformed/unsupported AML, FADT legacy and
  extended GAS plans, invalid SLP types, and shutdown tier ordering/failure
  behavior.
- `acpi_q35_shutdown`: GRUB Multiboot2 q35 boot; requires QEMU to exit normally
  with status 0 after the S5 write, plus inventory/S5/platform-handler markers.
- `acpi_q35_malformed_rsdp`, `acpi_q35_malformed_aml`: synthetic corrupt RSDP
  and malformed AML boot regressions; must reject, log, and still reach the boot
  marker.
