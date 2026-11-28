# Astia loadable modules

Loadable driver sources belong under this directory. Kernel-owned module
loading lives under `kernel/src/mod`; the public C++ module contract is
`kernel/include/astia/module.hpp`.

ET_REL module entrypoints use C++ linkage and the exact coroutine signature
`astia::core::task<void> module_main()`. A successful entry returns
`astia::error_t::enone`; any other value fails the run. The synchronous
`int module_main()` form is not part of the ABI. Driver registration, PCI access, DMA ownership, interrupt
routes, user-buffer pinning, and logging are exposed as typed C++ interfaces
under `astia::module` and the kernel device namespaces. There are no C module
facade headers or compatibility aliases.

The `tests/` subtree contains small C++ ET_REL fixtures used by the kernel ELF
loader tests. Hardware drivers use one directory per driver, such as
`modules/nvme`. Transport protocol scaffolds live in `modules/udp` and
`modules/tcp`; both own a `net::protocol` handler and are unloadable.

The `usb/` subtree is a bounded C++ USB protocol foundation and a read-only
xHCI PCI capability-probe ET_REL skeleton. It intentionally does not provide
USB class drivers or transfer-ring hardware support.
