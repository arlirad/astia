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
creates one polled I/O queue, and supports aligned reads up to 8192 bytes.
Writes return read-only errors through the generic block-device contract. MSI-X
is explicitly disabled: although the x86 interrupt layer can own device
vectors, the generic PCI core does not yet bind a device capability to that
router and program an MSI/MSI-X message. Completion polling remains the safe
fallback until that binding is complete.

`run-qemu.sh` builds both `astiakrn_runtime` and `nvme_module`, packages the
ET_REL object into an uncompressed newc initrd, attaches `fixture.raw` to
QEMU's emulated NVMe controller, and verifies the namespace and known-sector
read from `/dev/nvme0n1` through the module self-test.
