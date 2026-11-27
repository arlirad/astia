# Astia loadable module contract

## Header layout

| Header | Contract |
|---|---|
| `<astia/module.hpp>` | Generic lifecycle and typed ownership: `module_main`, PCI/MSI-X helpers, net device aliases, copy-in/out, logging, `make_resident()`. |
| `<astia/dev/capabilities.hpp>` | Generic capabilities: granted PIO windows (`io_port_window`, `claim_io_window`), IRQ lines (`irq_line`), module-owned routes (`irq_route`). |
| `<astia/dev/uart_platform.hpp>` | UART platform capability line. |
| `<astia/dev/tty_input.hpp>` | Input sink (`tty_input_write`). |
| `<astia/mm/*>`, `<astia/io.hpp>`, `<astia/net.hpp>` | Memory, I/O and network contracts as before. |

The generic module header carries no device-specific entry points. A device
module owns its device identity (line numbers, port addresses, protocol and
decoding) and includes the generic capability headers it uses. A new kernel
capability is added to the capability header, not to `module.hpp`.

Modules state the resources they need; the platform grants them only from its
resource policy. A module never receives the raw bus, and never a port range
it did not ask for: `claim_io_window(first, last)` returns an invalid window
when the request is not fully inside a granted range.

`astia::module::msix_route`, `astia::module::user_pin`, and
`astia::mm::dma_buffer` are move-only ownership types; release, unpin,
cancellation, and completion invalidate their state. The kernel owns PCI
devices, mapped BARs, interrupt routes, DMA frames, user-buffer pins,
registered files, and asynchronous bridges. A module must not retain one of
these handles after its matching release, unpin, cancellation, or completion.
`astia::module::read_registered` and `write_registered` are task-shaped and
never spin while a device completion is outstanding.

## Entry point and loading

Every loadable ET_REL object defines:

```cpp
int module_main();
```

The loader resolves its exact Itanium C++ symbol `_Z11module_mainv`. The
return type is `int`; it is not encoded in that symbol name. The loader
validates, relocates, and registers the image without calling the entrypoint.
Running marks the image running, invokes every validated `.init_array`
function in array order, then calls the entrypoint synchronously. A negative
result is a module error convention; the loader logs it but does not
reinterpret it as an exception or coroutine failure. Exit invokes validated
`.fini_array` functions in reverse order and clears the running state.

Module entrypoints are not `astia::core::task` functions; the loader does not
await or resume them. The loader bounds both constructor arrays, treats an
unsuffixed array section as priority 65535, sorts numeric
`.init_array.N`/`.fini_array.N` sections in ascending priority, and retains
section-table order for ties. Each non-null entry must point into an allocated
executable section; unsupported pre-initialization or legacy constructor
sections are rejected.

## Lifetime and references

A module claim on a capability takes a kernel-held reference on the calling
module, and every guarded dispatch takes and drops one for its duration. While
those references exist, `mod::close()` waits for them and for dependent
modules; only then does it run finalizers and unmap the image. Close waiters
resume through the resume queue. A waiter that becomes ready in interrupt or
module-callback context is parked and delivered by the normal-context
`service_module_waits()` pump, so finalizers and the final unmap never run in
interrupt context.

## Residency

`astia::module::make_resident()` resolves the calling module from the
execution context and marks it permanently non-unloadable. A subsequent close
returns `resident` without running fini handlers or unmapping. Residency is
resolved for the caller only; a module cannot request it for another image.

## Guarded IRQ routes

The guarded `arch::register_irq_handler(irq, handler, enter, leave, context)`
overload calls `enter` before the handler, runs the handler only when `enter`
returns true, then calls `leave`. The EOI is unconditional, including for a
refused or masked dispatch. `arch::mask_irq_handler(irq)` masks the line while
keeping the claim; `arch::unmask_irq_handler(irq)` reopens it.
`arch::unregister_irq_handler(irq)` clears the binding and refuses while a
dispatch is in flight, so the caller can retry after references drain.

`dev::irq_line` is the kernel's own unguarded line: a kernel-owned handler and
no module reference (uart16550 uses it). `dev::irq_route` is the module-facing
form: `claim()` resolves the calling module, holds a reference for the claim,
and guards every dispatch with that reference; `release()` masks the line,
retries unregistration, and drops the claim. A route is a value constructed
with its line, e.g. `astia::dev::irq_route route{1}`. One route owns a line.

`dev::io_port_window` is a restricted byte-access window over a granted
absolute port range. A module asks for the range it owns with
`dev::claim_io_window(first, last)`; the platform grants only ranges in its
policy. The raw `pio_bus` is kernel-private.
`dev::tty_input_write()` is the input sink into the terminal line discipline.

The PS/2 keyboard module is the example consumer: it owns line 1 and the 8042
port pair, calls `route.claim(handler)`, reads its ports through the platform
window, forwards decoded bytes with `tty_input_write`, and marks itself
resident.

## Relocation and placement

`astia::elf::loader::audit_relocatable()` runs before mapping any module.
Kernel modules are placed above 4 GiB through the kernel direct map, so
`R_X86_64_32`/`R_X86_64_32S` are rejected with a diagnostic naming
`-mcmodel=large`, and relocation types outside the module ABI are rejected as
unsupported. Images live in the direct map, so module text and data are
executable and addressable from every process address space. The ABI applies
RELA relocations only; a `SHT_REL` section patching an allocated section is
rejected.

## Verification

`modules/tests` compiles the public headers independently and builds typed
ET_REL fixtures. `kernel/tests/elf_test.cpp` verifies symbol resolution,
relocation auditing, entrypoint execution, and malformed-image rejection.
`kernel/tests/module_lifecycle_test.cpp` covers ownership, route claims,
residency, deferred close delivery, and claim rollback.
`kernel/tests/interrupts_guard_test.cpp` covers enter/handler/leave/EOI
ordering, masking, and unregister refusal. The PS/2 QEMU proof boots the
module, asserts `ready irq=1`, and injects keystrokes (`PS2_READY`,
`PS2_KEYS:abc`).
