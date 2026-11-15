# NVMe loadable module

`nvme.cpp` is compiled as an x86-64 ET_REL module and copied into the QEMU
initrd. The `mod` init step loads and runs its `module_main` before PCI
enumeration. The module registers a PCI driver through
`astia_module_register_pci_block_driver`; the generic kernel adapter owns the
shared block-device group and exposes stable namespace paths such as
`/dev/nvme0n1` and `/dev/nvme0n2`.

The module uses the public C ABI for PCI configuration and BAR mapping, DMA
page allocation, user-buffer pinning/copying, and block I/O. The ABI keeps C++
ownership and virtual dispatch in the kernel while module code owns only its
opaque controller context. Loaded modules remain resident while registered
drivers or devices reference their callbacks, and running modules cannot be
unloaded.

Controller discovery issues Identify Controller, then walks the NVMe active
namespace ID list (CNS 02h). Each namespace is identified independently and
registered with a path derived from its NSID, so enumeration order cannot
renumber an existing namespace. The adapter retains a shared controller
lifetime until every namespace block and its device-file references are gone.

Reads and writes support aligned transfers up to two `ASTIA_PAGE_SIZE` pages
(8192 bytes on the x86-64 ABI), including async
MSI-X and polling fallback paths. Async cancellation quarantines bridge slots
until completion or controller teardown. Flush is issued per namespace, and
write errors, invalid geometry, and unknown namespace paths are rejected. A
controller timeout or fatal status resets and reinitializes the admin and I/O
queues, re-identifies every namespace, and preserves the stable namespace
contexts before I/O resumes. Outstanding asynchronous requests are completed
with an error during recovery. All DMA and MSI-X resources are released during
teardown.

`run-qemu.sh` creates two deterministic raw namespace fixtures, builds both
normal MSI-X and forced-polling configurations, and verifies `/dev/nvme0n1`,
`/dev/nvme0n2`, async read/write/flush persistence, controller reset recovery,
invalid geometry, unknown namespace errors, and interrupt/fallback behavior.
