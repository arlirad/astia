# Astia module ABI

The public module ABI is declared by `kernel/include/astia/module_abi.h` and
uses C linkage. `ASTIA_MODULE_ABI_VERSION_MAJOR` is incremented for an
incompatible change; `ASTIA_MODULE_ABI_VERSION_MINOR` is incremented for an
additive change. The current ABI is version 1.1. Version 1.1 appends the
optional `namespace_count` and `namespace_info` callbacks to the block-driver
table; a null pair keeps the single-namespace behavior. Modules must be
rebuilt for every ABI version change because registration receives a pointer
to the complete table and does not carry a runtime table length.

The ABI is the x86-64 C ABI: pointers are 64 bits, `uint8_t`, `uint16_t`,
`uint32_t`, and `uint64_t` retain their standard widths, and transfer results
use `int64_t`. The public header contains compile-time size and `offsetof`
checks for every exported structure. Named `reserved` fields occupy bytes
that would otherwise be implicit padding and must be zero when a module
creates a structure. Do not use packed or compiler-specific structure
attributes.

The block-driver table is 128 bytes. Its final callbacks are at offsets 112
and 120; the preceding read, asynchronous, write, and flush callbacks remain
at their existing offsets. This preserves the NVMe and VirtIO write, flush,
and asynchronous callback ABI while making the namespace extension explicit.

The kernel owns PCI devices, mapped BARs, interrupt routes, DMA frames, user
buffer pins, registered files, and asynchronous request bridges. Module
contexts and callback tables remain valid until the registered driver's remove
callback returns. A module must not retain a PCI, DMA, pin, MSI-X route, or
request handle after its matching release, unpin, cancellation, or completion.
Release functions clear owned output handles and reject stale or malformed
handles where the ABI can identify them.

Invalid or stale MSI-X routes and asynchronous request handles are rejected.
DMA and pin release operations are idempotent after their output handle has
been cleared; callers must not copy or fabricate handles.

All lengths are byte counts and are bounded by the operation's device or page
limits. Zero-length and null-buffer transfers are invalid. Block transfers
must be aligned to the device block size and remain within the reported
device size. MMIO offsets must be naturally aligned for the access width.

The C and C++ layout tests under `modules/tests` compile the public header
independently. The C fixture is compiled as a separate freestanding module,
which verifies that a C module can consume the ABI without C++ declarations.
