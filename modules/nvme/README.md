# Kernel-owned NVMe driver

The concrete read-only driver is kernel-owned at `kernel/src/dev/nvme/` and
is registered through the PCI driver manager as a block device at
`/dev/nvme0n1`. It discovers every PCI function with class `01`, subclass
`08`, programming interface `02`; no vendor or device ID is selected.

The current loadable-module ABI cannot safely host this C++ driver. The loader
only resolves explicitly exposed ELF symbols, and the kernel exposes no NVMe,
PCI, block-device, MMIO, frame-allocation, pinned-buffer, filesystem, or C++
runtime ABI. It also provides only a zero-argument `module_main()` entry and
no lifetime/unload contract for registered devices. Loading a C++ object would
therefore fail unresolved relocations or require an unsafe private symbol
surface, so this implementation is intentionally kernel-owned.

`run-qemu.sh` creates a raw image with a known first sector, builds the kernel,
attaches it to QEMU's emulated NVMe controller, and checks the Identify,
namespace, and known-sector read log lines.

The verified slice deliberately masks MSI-X and polls completion phases with a
bounded scheduler-yield/timer wait. Astia's PCI interrupt core has no MSI/MSI-X
vector router yet; QEMU can raise an MSI-X vector, but no kernel handler can
route it to this controller. Enabling MSI-X here would therefore be unsafe.
