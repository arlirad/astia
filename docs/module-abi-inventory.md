# Module contract and lifetime inventory

## Loader contract

| Item | Definition | Lifetime rule |
| --- | --- | --- |
| Image | x86-64 ELF `ET_REL` | The loader owns one contiguous relocated image until `unload_module` succeeds. |
| Entry point | C++ `int module_main()` | `run_module` marks the image running before a synchronous call; a running image cannot be unloaded. |
| Symbols | Linked kernel export table | Imported addresses are valid only while the matching kernel build is running. |
| Registration | `astia::hw::driver_registry` and endpoint registries | Registration objects and callback tables must remain in the image for the whole registered lifetime. |
| Removal | `hw::driver::remove`, `exit_module`, and endpoint unregister functions | Driver and endpoint removal must complete before `exit_module`; finalizers run in reverse order before `unload_module`. |

Modules remain resident after `module_main` returns because their driver
objects, callback tables, interrupt handlers, and device contexts may still be
referenced. The owner must unregister those resources, call `exit_module`, and
then call `unload_module`. `loaded_module*` values are registry addresses and
are invalid after unload or registry compaction.

## Public header

`kernel/include/astia/module.hpp` is the sole public loadable-module header.
It contains typed PCI, MSI-X, user-memory, logging, and registered-I/O
operations. It intentionally provides no C headers, C entrypoint aliases, or
compatibility facade.

The module entrypoint is a global C++ function with signature
`int module_main()`. The ELF loader matches `_Z11module_mainv`; it does not
encode or infer the return type from the mangled name.

## I/O and address-space ownership

Asynchronous module I/O creates a ring owned by the current process. The ring
retains that process until destruction, and every parked operation retains its
open file and pinned buffer list. Deferred readiness is consumed by the owner
process; IRQ paths set readiness state and request a reschedule instead of
resuming device code inline.

Completion copies use the ring owner rather than whichever process executes
the deferred worker. User mappings are pinned until operation cleanup;
completed and canceled slots release pins, file references, and device state in
task context.

## Validation inventory

- `kernel/src/mod/module.cpp`: ET_REL allocation, relocation, constructor and
  destructor arrays, registry, and unload policy.
- `kernel/src/mod/module_driver.cpp`: typed PCI/MSI-X, DMA/user-buffer,
  logging, and registered-I/O helpers.
- `kernel/src/elf/reloc_loader.cpp`: C++ entrypoint and symbol resolution.
- `modules/tests/typed_*`: typed contract and header fixtures.
- `kernel/tests/elf_test.cpp`: ET_REL and executable loader coverage.
- `modules/virtio-blk/run-qemu.sh`: read/write, flush, read-only, and attached
  process kernel-buffer regression.
- `modules/nvme/run-qemu.sh`, `modules/rtl8139/run-qemu.sh`, and
  `modules/usb/run-qemu.sh`: typed module consumers and device teardown paths.
