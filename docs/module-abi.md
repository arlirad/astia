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

## Lifetime tokens and guarded IRQ routes

`astia::module::lifetime_token` is a move-only lease on the calling module's
image, acquired with `lifetime_token::acquire()` from `module_main()` context.
While a token is live, `mod::close()` waits for it; a resident module's close
returns `resident` without running finalizers or unmapping. Releasing the last
lease or guarded callback reference can complete a pending close. A token that
cannot resolve an active module, or whose acquisition is refused after
quiescing, is invalid.

`lifetime_token::try_enter()` acquires a guarded callback reference and
returns false once teardown has started; `leave()` releases it and is always
called after a successful `try_enter()`, including after `release()`. Both are
safe in interrupt context and perform no allocation or blocking.

A module-owned IRQ handler is claimed with the guarded
`arch::register_irq_handler(irq, handler, enter, leave, context)` overload.
The kernel calls `enter` before the handler and runs the handler only when
`enter` returns true; `leave` runs after a successful entry and the EOI is
unconditional. The PS/2 module uses this through
`astia::module::ps2_claim_irq(handler, token)`, `ps2_platform_ports()`, and
`ps2_unmask_irq()`. `arch::mask_irq_handler(irq)` masks the line while keeping
the claim, and `arch::unregister_irq_handler(irq)` clears the binding, refusing
while a dispatch is in flight so the caller can retry after references drain.

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
