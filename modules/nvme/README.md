# NVMe loadable module

`nvme.cpp` is compiled as an x86-64 ET_REL module and copied into the QEMU
initrd. The `mod` init step loads and runs its `module_main` before PCI
enumeration. The module registers a PCI driver through
`astia_module_register_pci_block_driver`; the generic kernel adapter owns the
shared block device and exposes it as `/dev/nvme0n1`.

The module uses the public C ABI for PCI configuration and BAR mapping, DMA
page allocation, user-buffer pinning/copy-out, and block I/O. The
ABI keeps C++ ownership and virtual dispatch in the kernel while module code
owns only its opaque controller context. Loaded modules remain resident while
registered drivers or devices reference their callbacks, and running modules
cannot be unloaded.

The optional async submit/cancel ABI parks the stable `io_request` bridge,
submits the NVMe command without waiting, and completes it from the MSI-X
handler through the kernel wake path. Completion-owned bridge slots remain
valid until the controller reports the command or cancellation has quarantined
them; device removal cancels all slots before the module callback context is
released. Synchronous reads and writes remain the compatibility paths when
MSI-X routing is unavailable.

The driver accepts NVMe class `01/08/02` PCI functions, identifies namespace 1,
creates one I/O queue, and supports aligned reads and writes up to 8192 bytes.
Writes set FUA and the self-test issues an explicit Flush command before
reopening the device and reading the written sector back. Modules without a
write callback retain the generic EROFS fallback. When the
PCI capability and arch-neutral vector router are available, the module
requests one vector, programs MSI-X table entry zero, enables the completion
queue interrupt, and releases the route on teardown. The completion handler
only arms a wake flag; queue ownership and CQ-head acknowledgement remain in
the bounded I/O path. Missing capability, invalid routing, or setup failure
selects the polling fallback without enabling MSI-X.

Transfers are checked for namespace geometry and page limits before command
submission. PRP2 is populated for a second page; larger or malformed requests
are rejected. Bounded command polling detects timeout and CSTS fatal status,
clears CC.EN, waits for CSTS.RDY to clear, and marks the controller unusable
instead of pretending recovery succeeded. Pending async requests are canceled
and quarantined before controller/module teardown.

`run-host.sh` compiles deterministic host tests for command encoding, geometry,
PRP boundaries, completion status, and request lifetime transitions.

`run-qemu.sh` builds both `astiakrn_runtime` and `nvme_module`, packages the
ET_REL object into an uncompressed newc initrd, attaches `fixture.raw` to
QEMU's emulated NVMe controller, and verifies known-sector read, asynchronous
write, FUA/Flush, reopen/read persistence, and invalid alignment/range requests
from `/dev/nvme0n1` through the module self-test. It runs both the normal MSI-X
case and a compile-time forced-polling case.
