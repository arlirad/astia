# Astia loadable modules

Block module sources use the typed `dev::blk`, `astia::io`, and
`mm::dma_buffer` contracts. The loader still consumes C++ ET_REL objects with
the C-linkage `module_main` entry point and owns device/filesystem registration.

PCI, IRQ/MSI-X, network, USB, and user-buffer C headers remain staged
follow-up work. They are not block contracts and are intentionally not mixed
into this slice.

The complete ABI and module-lifetime inventory is in
`docs/module-abi-inventory.md`.
