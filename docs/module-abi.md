# Astia loadable module contract

The public module interface is the C++ header
`kernel/include/astia/module.hpp`. It exposes typed ownership and device
operations under `astia::module`; semantic device, driver, packet, and
registry contracts remain C++ interfaces. The old C module headers and C
facade aliases are removed.

## Entry point and loading

Every loadable ET_REL object defines the C++ function:

```cpp
int module_main();
```

The loader resolves its exact Itanium C++ symbol `_Z11module_mainv`. The
return type is `int`; it is not encoded in that symbol name. `load_module`
relocates and registers the image without calling the entrypoint.
`run_module` marks the image running, invokes every validated `.init_array`
function in array order, then calls the `int (*)()` entrypoint synchronously.
A negative result is a module error convention; the loader logs it but does
not reinterpret it as an exception or coroutine failure. `exit_module` invokes
validated `.fini_array` functions in reverse order and clears the running
state so teardown can be followed by `unload_module`.

Module entrypoints are not `astia::core::task` functions. The loader does not
await or resume a task, and it does not inspect task completion or error state.
Asynchronous device requests are managed by the typed I/O and driver
contracts; changing module startup to an async lifecycle is a separate
follow-up.

The loader bounds both constructor arrays, treats an unsuffixed array section
as priority 65535, and sorts numeric `.init_array.N`/`.fini_array.N` sections
in ascending priority while retaining section-table order for ties. Runtime
finalization then traverses the sorted fini array in reverse, as required by
the ELF ABI. Non-numeric or overflowing priority suffixes are rejected.
Each non-null entry must point into an allocated executable section, and
unsupported pre-initialization or legacy constructor sections are rejected.

## Typed ownership

`astia::module::msix_route`, `astia::module::user_pin`, and
`astia::mm::dma_buffer` are move-only ownership types. Release, unpin,
cancellation, and completion paths invalidate their owned state. Module
contexts and callback tables remain valid until the registered driver's remove
callback returns.

The kernel owns PCI devices, mapped BARs, interrupt routes, DMA frames,
user-buffer pins, registered files, and asynchronous request bridges. A
module must not retain one of these handles after its matching release,
unpin, cancellation, or completion.

`astia::module::read_registered` and
`astia::module::write_registered` are task-shaped: they return
`core::task<i64>`, await the filesystem/device operation, and return 0 for a
full-length transfer, a positive short count, or a negative errno. They never
spin or halt the CPU while a device completion is outstanding.

## Kernel-owned route references and residency

The module does not hold a public lifetime object. The kernel resolves the
calling module from the execution context and owns the reference implied by a
claimed route: `astia::module::ps2_claim_irq(handler)` refuses outside module
entrypoint context and takes an active reference on the module before binding
IRQ1. Every guarded dispatch enters through the kernel's `enter` and leaves
through `leave`, taking and dropping that same reference; the claim itself is
also a hold until `ps2_release_irq()`. While either exists, `mod::close()`
waits, and a resident module's close returns `resident` without running
finalizers or unmapping.

`astia::module::make_resident()` resolves the calling module itself and marks
it permanently non-unloadable. If a close waiter becomes ready in interrupt or
module-callback context, the kernel parks it and delivers it only from the
normal-context `service_module_waits()` pump, so finalizers and the final unmap
never run from an interrupt.

A module-owned handler is bound with the guarded
`arch::register_irq_handler(irq, handler, enter, leave, context)` overload.
The handler runs only when `enter` returns true, and EOI is unconditional. The
PS/2 module uses the kernel-owned route through `ps2_claim_irq(handler)`,
`ps2_platform_ports()`, `ps2_unmask_irq()`, and `ps2_release_irq()`.
`arch::mask_irq_handler(irq)` masks the line while keeping the claim, and
`arch::unregister_irq_handler(irq)` clears the binding, refusing while a
dispatch is in flight so the caller can retry after references drain.

## Relocation contract

`astia::elf::loader::audit_relocatable()` runs before mapping any module.
Kernel modules are placed above 4 GiB through the direct map, so
`R_X86_64_32`/`R_X86_64_32S` are rejected with a diagnostic naming
`-mcmodel=large`; relocation types outside the module ABI are rejected as
unsupported.

Module images are allocated through the kernel direct map, so their code and
data remain executable and addressable from every process address space.
Because the module ABI applies RELA relocations, the audit also rejects a
`SHT_REL` section that patches an allocated section.

## Verification

`modules/tests` compiles the public C++ header independently and builds typed
ET_REL fixtures. `kernel/tests/elf_test.cpp` verifies C++ symbol resolution,
relocations, entrypoint execution, and malformed-symbol rejection.
Driver-specific host, teardown, and QEMU tests cover NVMe, VirtIO, RTL8139,
USB, and PS/2 keyboard consumers.
