# Astia module boundary

Loadable modules are C++ ET_REL objects. The loader boundary is deliberately
small: `module_main` has C linkage, the ELF image is relocated, and undefined
symbols resolve against the linked kernel symbol table. Driver semantics are
not described by a C callback ABI.

Block modules use the public C++ contracts: `astia::io::request` and
`astia::io::flight` own request lifetime and cancellation state,
`astia::mm::dma_buffer` owns contiguous DMA pages with RAII,
`astia::dev::blk::blkdev` applies block geometry policy. Hardware discovery and
driver lifetime are owned by the neutral `astia::hw::driver_registry`;
`hw::driver` probes physical bus devices and publishes endpoint products. The
registry does not know block, character, network, or filesystem policy.

The block path has no callback aggregate and no `async_submit`, `async_cancel`,
or `async_poll` function-pointer contract. Device implementations park the
same typed request, retain typed controller state, notify the common I/O
engine on IRQ completion, and finish or cancel it on request resume.

The dependency graph is `hw` bus/device/driver registry -> endpoint products
(`dev::blk`/`dev::chr`, or future independent `net` products) -> neutral
`io` request/flight primitives. The network subsystem is not a `dev` child and
will retain its packet/backpressure contract when migrated.
PCI is a hardware bus contract in `astia/hw/pci.hpp`; `dev` owns only endpoint
interfaces and filesystem adapters.

The remaining `module_abi_*.h` files are staged follow-up boundaries only:
`module_abi_core.h` carries loader entry/version constants, while
`module_abi_pci.h`, `module_abi_interrupt.h`, `module_abi_memory.h`,
`module_abi_net.h`, and `module_abi_usb.h` remain for the PCI, MSI-X,
user-buffer, network, and USB slices. Their migration is intentionally not
mixed into this block + DMA change. The former `module_abi.hpp` and C
block-driver/request layouts were removed rather than retained as aliases.

`astia::io` is neutral to device class. Character devices keep stream and TTY
policy; block devices add alignment, capacity, and flush policy; network will
reuse request ownership primitives while retaining packet ownership,
backpressure, and IRQ/deferred semantics in a follow-up contract.

The complete header, symbol, ownership, unload, and validation inventory is
in `docs/module-abi-inventory.md`.
