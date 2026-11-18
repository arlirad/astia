# Astia module ABI

The primary module API is the C++ header `kernel/include/astia/module_abi.hpp`.
It provides typed block-driver descriptors, RAII DMA ownership, shared-owner
aliases, and the C++ registration adapter used by the NVMe and VirtIO modules.
The freestanding C headers under `kernel/include/astia` are a compatibility
shim for the loader entry point and foreign modules: `module_abi_core.h`
contains version and platform contracts, while `module_abi_pci.h`,
`module_abi_memory.h`, `module_abi_io.h`, and `module_abi_interrupt.h` retain
the ABI 1.1 layouts and symbols. `module_abi.h` includes all shim headers for
source compatibility. `module_main` remains the C-linkage ET_REL loader
entry; the C++ registration adapter preserves the same exported symbols.
`ASTIA_MODULE_ABI_VERSION_MAJOR` is incremented for an
incompatible change; `ASTIA_MODULE_ABI_VERSION_MINOR` is incremented for an
additive change. The current ABI is version 1.1. Version 1.1 appends the
optional `namespace_count` and `namespace_info` callbacks to the block-driver
table; a null pair keeps the single-namespace behavior. Modules must be
rebuilt for every ABI version change because registration receives a pointer
to the complete table and does not carry a runtime table length.

ET_REL modules are built with static-destructor registration disabled because
the loader owns module lifetime and does not run C++ termination arrays.
Drivers still release DMA, interrupt, and asynchronous resources explicitly
before their remove callback returns; local C++ RAII wrappers remain valid for
scoped ownership.

The ABI is the x86-64 C ABI: pointers are 64 bits, `uint8_t`, `uint16_t`,
`uint32_t`, and `uint64_t` retain their standard widths, and transfer results
use `int64_t`. The public header contains compile-time size and `offsetof`
checks for every exported structure. Named `reserved` fields occupy bytes
that would otherwise be implicit padding and must be zero when a module
creates a structure. Do not use packed or compiler-specific structure
attributes.

`ASTIA_PAGE_SIZE` is the compile-time memory-page contract shared by the
kernel MM, module DMA allocation, user-buffer pinning, and protocol helpers.
The current x86-64 ABI defines it as 4096 bytes. The public header emits a
compile-time error for non-x86-64 targets rather than claiming that 4096 is
universal; a future architecture must add an explicit ABI and kernel-MM
definition before modules can be built for it.

## Temporary kernel symbol surface

Until a deliberate kernel-module export policy is introduced, ET_REL modules
may resolve every defined `STB_GLOBAL` or `STB_WEAK` kernel symbol whose ELF
visibility is `STV_DEFAULT`. The build generates this resolver table from the
linked kernel image, preserving the exact symbol spelling (including C++
mangling) and linked address. Local, hidden, protected, and undefined symbols
are not visible to modules. This broad surface is temporary and is not a
module-to-module dependency mechanism; modules still cannot resolve symbols
from another module.

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

Module callbacks run in one of two contexts, reported by
`astia_module_get_callback_context()`. `match`, `probe`, `remove`, the
synchronous block callbacks, asynchronous submit/cancel/poll, and selftests
run in process context and may block. An MSI-X interrupt handler runs in IRQ
context: it must not block, call the filesystem, allocate, or invoke a
synchronous block callback. A module may inspect device completion state there
and call `astia_module_io_complete()`: the kernel records the result, wakes
the parked operation, and returns `-EAGAIN`; the actual completion is applied
when the next process-context poll resumes. Every accepted request completion
is one-shot: a second completion returns `-EALREADY`, and cancellation or
removal invalidates the request handle. Module callbacks must not retain the
device or callback context after `remove` returns.

The `astia_module_io_request.opaque` value is a kernel-issued, generation
validated request handle, not a pointer. It identifies both the registering
module and one bridge slot. The kernel rejects fabricated, copied-to-the
wrong-device, stale, canceled, and reused-slot handles before dispatching a
completion. A module must pass the value back unchanged and must not inspect,
write, or manufacture it. The handle remains valid only between successful
asynchronous submission and completion or cancellation; destruction of the
owning device cancels all outstanding bridges before `remove` is called.

MSI-X route pointers are opaque ownership tokens even though the ABI retains
the 1.1 route layout. The kernel records the device, route geometry, and a
generation cookie in the route's reserved bytes. Every route operation
validates the token, generation, device, and 16-byte table-entry bounds.
Copied, stale, double-released, wrong-device, and malformed routes are
rejected. Modules must initialize a route to zero, pass the same route object
to its release operation, and never write the route after a successful
request.

All lengths are byte counts and are bounded by the operation's device or page
limits. Zero-length and null-buffer transfers are invalid. Block transfers
must be aligned to the device block size and remain within the reported
device size. MMIO offsets must be naturally aligned for the access width.

The C and C++ layout tests under `modules/tests` compile the public header
independently. The C fixture is compiled as a separate freestanding module,
which verifies that a C module can consume the ABI without C++ declarations.
