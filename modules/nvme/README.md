# NVMe loadable module

`nvme.cpp` is compiled as an x86-64 ET_REL module and copied into the QEMU
initrd. The `mod` init step loads and runs its `module_main` before PCI
enumeration. The module registers a PCI driver through
`astia_module_register_pci_block_driver`; the generic kernel adapter owns the
shared block device and exposes it as `/dev/nvme0n1`.

The module uses the public C ABI for PCI configuration and BAR mapping, DMA
page allocation, user-buffer pinning/copy-out, and read-only block I/O. The
ABI keeps C++ ownership and virtual dispatch in the kernel while module code
owns only its opaque controller context. Loaded modules remain resident while
registered drivers or devices reference their callbacks, and running modules
cannot be unloaded.

The driver accepts NVMe class `01/08/02` PCI functions, identifies namespace 1,
creates one I/O queue, and supports aligned reads up to 8192 bytes. Writes
return read-only errors through the generic block-device contract. When the
PCI capability and arch-neutral vector router are available, the module
requests one vector, programs MSI-X table entry zero, enables the completion
queue interrupt, and releases the route on teardown. The completion handler
only arms a wake flag; queue ownership and CQ-head acknowledgement remain in
the bounded read path. Missing capability, invalid routing, or setup failure
selects the polling fallback without enabling MSI-X.

`run-qemu.sh` builds both `astiakrn_runtime` and `nvme_module`, packages the
ET_REL object into an uncompressed newc initrd, attaches `fixture.raw` to
QEMU's emulated NVMe controller, and verifies the namespace and known-sector
read from `/dev/nvme0n1` through the module self-test. It runs both the normal
MSI-X case and a compile-time forced-polling case.
