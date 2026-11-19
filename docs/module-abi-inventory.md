# Module ABI and lifetime inventory

## Loader contract

| Item | Definition | Lifetime rule |
| --- | --- | --- |
| Image | x86-64 ELF `ET_REL` | The loader owns one contiguous relocated image until `unload_module` succeeds. |
| Entry point | `module_main` from `module_abi_core.h` | `run_module` marks the image running before entry and a running image cannot be unloaded. |
| Symbols | Linked kernel export table | Imported addresses are valid only while the matching kernel build is running. |
| Registration | `astia::hw::driver_registry` and endpoint registries | Registration objects and callback tables must remain in the image for the whole registered lifetime. |
| Removal | `hw::driver::remove` and endpoint unregister functions | Removal must complete before an image can be unloaded; the current loader keeps run modules non-unloadable. |

There is no module unload callback. A module that returns from `module_main`
still remains resident because its driver objects, callback tables, interrupt
handlers, and device contexts may still be referenced. `unload_module` is
therefore allowed only for an image whose entry point has never been run.
`loaded_module*` values are registry addresses and are invalid after unload or
registry compaction.

## Header inventory

| Header | Contract |
| --- | --- |
| `module_abi_core.h` | ABI version, page size, static layout checks, and `module_main`. |
| `module_abi_pci.h` | PCI device view, probe/remove callbacks, BAR access, and bus mastering. |
| `module_abi_interrupt.h` | MSI-X route ownership, interrupt callback context, logging, and selftest entry. |
| `module_abi_memory.h` | DMA allocation/release, user pin tokens, and bounded copy helpers. |
| `module_abi_io.h` | Registered synchronous and asynchronous block I/O entry points. |
| `module_abi_net.h` | Packet views, TX reclaim, RX IRQ/deferred callbacks, and network registration. |
| `module_abi_usb.h` | USB probe/remove callbacks and TTY input forwarding. |
| `module_abi.h` | Aggregate include; it does not add another ABI surface. |

The typed C++ block contract is separate from the C headers. It consists of
`io::request`, `io::flight`, `mm::dma_buffer`, `dev::blk::blkdev`, and
`hw::driver`. A module must not retain a request, DMA buffer, endpoint product,
or callback context after its owning remove path has completed.

## I/O and address-space ownership

Asynchronous module I/O creates a ring owned by the current process. The ring
retains that process until destruction, and every parked operation retains its
open file and pinned buffer list. Deferred readiness is only consumed by the
owner process; IRQ paths set readiness state and request a reschedule instead
of resuming device code inline.

Module calls that use kernel buffers explicitly enable the internal
kernel-buffer ring mode. Attached processes still validate ordinary ring
buffers as user ranges. Kernel buffers are accepted only for that marked
ring
and are required to be in the kernel virtual-address half. This prevents a
ring-3 SQE from turning a kernel pointer into a writable buffer while allowing
module read/write selftests to run while an address space is attached.

Completion copies use the ring owner rather than whichever process happens to
execute the deferred worker. User mappings are pinned until operation cleanup;
completed and canceled slots release pins, file references, and device state in
task context.

## Deferred and cancellation rules

Readiness, timeout, poll, and sleep events only mark a parked operation. The
owner's scheduler turn drains those marks. Process exit and exec destroy
owned rings, cancel parked requests, unmap ring aliases, and release resources
before the owner reference is dropped.

Each parked request has a nonzero generation. Deferred coroutine wakeups carry
that generation; cancellation removes only the matching queued continuation
and waits for a matching running continuation to finish. A reused coroutine
address or ring slot cannot consume an old wakeup.

## Validation inventory

The source and test inventory for this boundary is:

- `kernel/src/mod/module.cpp`: ET_REL image allocation, relocation, registry,
  and unload policy.
- `kernel/src/mod/module_driver.cpp`: registration adapters, device removal,
  DMA/user-buffer wrappers, and module lifetime checks.
- `kernel/src/sys/aio*.cpp`: ring ownership, pinning, deferred work, and
  completion publication.
- `modules/tests/typed_*`: typed contract and header layout fixtures.
- `modules/virtio-blk/run-qemu.sh`: read/write, flush, read-only, and attached
  process kernel-buffer regression.
- `modules/nvme/run-qemu.sh`, `modules/rtl8139/run-qemu.sh`, and
  `modules/usb/run-qemu.sh`: module ABI consumers and device teardown paths.
