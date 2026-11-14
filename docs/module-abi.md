# Astia module ABI

The public module ABI is declared by `kernel/include/astia/module_abi.h` and
uses C linkage. `ASTIA_MODULE_ABI_VERSION_MAJOR` is incremented for an
incompatible change; `ASTIA_MODULE_ABI_VERSION_MINOR` is incremented for an
additive change. The current ABI is version 1.0. Structures use fixed-width
integer types and their layout is part of the ABI. Modules must be rebuilt
when the major version changes.

The kernel owns PCI devices, mapped BARs, interrupt routes, DMA frames, user
buffer pins, registered files, and asynchronous request bridges. Module
contexts and callback tables remain valid until the registered driver's remove
callback returns. A module must not retain a PCI, DMA, pin, MSI-X route, or
request handle after its matching release, unpin, cancellation, or completion.
Release functions clear owned output handles and reject stale or malformed
handles where the ABI can identify them.

All lengths are byte counts and are bounded by the operation's device or page
limits. Zero-length and null-buffer transfers are invalid. Block transfers
must be aligned to the device block size and remain within the reported
device size. MMIO offsets must be naturally aligned for the access width.

The C and C++ layout tests under `modules/tests` compile the public header
independently. The C fixture is compiled as a separate freestanding module,
which verifies that a C module can consume the ABI without C++ declarations.
