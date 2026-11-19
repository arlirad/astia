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
`run_module` marks the image running, invokes the `int (*)()` entrypoint
synchronously, and returns its integer result. A negative result is a module
error convention; the loader logs it but does not reinterpret it as an
exception or coroutine failure. A running image cannot be unloaded.

Module entrypoints are not `astia::core::task` functions. The loader does not
await or resume a task, and it does not inspect task completion or error state.
Asynchronous device requests are managed by the typed I/O and driver
contracts; changing module startup to an async lifecycle is a separate
follow-up.

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

## Verification

`modules/tests` compiles the public C++ header independently and builds typed
ET_REL fixtures. `kernel/tests/elf_test.cpp` verifies C++ symbol resolution,
relocations, entrypoint execution, and malformed-symbol rejection.
Driver-specific host, teardown, and QEMU tests cover NVMe, VirtIO, RTL8139,
and USB consumers.
