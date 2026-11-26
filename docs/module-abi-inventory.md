# Module contract and lifetime inventory

## Loader contract

| Item | Definition | Lifetime rule |
| --- | --- | --- |
| Image | x86-64 ELF `ET_REL` | The loader owns one contiguous relocated image until `mod::unload` succeeds. |
| Entry point | C++ `core::task<void> module_main()` returning `astia::error_t` | Task-shaped entry; `run` awaits it with one active reference pinned; `enone` succeeds, other values fail the run. A suspended entry is restored to `loaded` on failure. |
| Symbols | Linked kernel export table | Imported addresses are valid only while the matching kernel build is running. |
| Registration | `astia::hw::driver_registry` and endpoint registries | Registration objects and callback tables must remain in the image for the whole registered lifetime. |
| Removal | `hw::driver::remove`, task-shaped `exit`, and endpoint unregister functions | Driver and endpoint removal must complete before `exit`; finalizers run synchronously inside the exit task, in reverse order, before `unload`/`close`. |

Modules remain resident after `module_main` completes because their driver
objects, callback tables, interrupt handlers, and device contexts may still be
referenced. The owner must unregister those resources, await `mod::exit(id)`,
and then call `mod::unload(id)` (or await the task-shaped `mod::close(id)`).
`module_id` is the stable public handle; `loaded_module*` values are
host-test-only registry addresses and are invalid after unload.

## Public header

`kernel/include/astia/module.hpp` is the sole public loadable-module header.
It contains typed PCI, MSI-X, user-memory, logging, and registered-I/O
operations. It intentionally provides no C headers, C entrypoint aliases, or
compatibility facade.

The module entrypoint is a global C++ coroutine with signature
`astia::core::task<void> module_main()` returning `astia::error_t`. The ELF loader matches
`_Z11module_mainv`, validates the coroutine-frame ABI, and rejects malformed
or synchronous entrypoints transactionally. `mod::run(id)` and
`mod::exit(id)` are stable-id `core::task<module_error>` drivers; they do not
hold the registry lock across an await and account active references across
their complete lifetimes.

Initializers, finalizers, `atexit`, `__cxa_atexit`, and `__cxa_finalize` are
synchronous no-yield callbacks. Their callback context belongs to the current
run/exit continuation and is not migrated across coroutine execution. Before
the scheduler exists, the boot one-step driver accepts only a completed run
task; a suspended entry is rejected and rolled back to the loaded state.

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
- `kernel/src/elf/loader/reloc.cpp`: C++ task-shaped entrypoint and symbol resolution.
- `modules/tests/typed_*`: typed contract and header fixtures.
- `kernel/tests/elf_test.cpp`: ET_REL and executable loader coverage.
- `modules/virtio-blk/run-qemu.sh`: read/write, flush, read-only, and attached
  process kernel-buffer regression.
- `modules/nvme/run-qemu.sh`, `modules/rtl8139/run-qemu.sh`, and
  `modules/usb/run-qemu.sh`: typed module consumers and device teardown paths.
