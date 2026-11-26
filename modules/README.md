# Astia loadable modules

Loadable driver sources belong under this directory. Kernel-owned module
loading lives under `kernel/src/mod`; the public C++ module contract is
`kernel/include/astia/module.hpp`.

ET_REL module entrypoints use C++ linkage and the exact signature
`int module_main()`. Driver registration, PCI access, DMA ownership, interrupt
routes, user-buffer pinning, and logging are exposed as typed C++ interfaces
under `astia::module` and the kernel device namespaces. There are no C module
facade headers or compatibility aliases.

The `devfs/` subtree is the resident device-namespace module. It implements the
`devfs` filesystem and `/dev` policy (`/tty`, `/ptmx`, `pts/N`, permissions and
metadata) on top of the generic device-node registry in
`kernel/include/astia/fs/device_node.hpp`. Its ET_REL image is embedded as a
required boot module and loaded by the normal module loader; boot fails if it
cannot register, mount `/dev`, or activate the namespace. Producers never
hardcode `/dev` paths in core; they register relative node descriptors.

The `tests/` subtree contains small C++ ET_REL fixtures used by the kernel ELF
loader tests. Hardware drivers use one directory per driver, such as
`modules/nvme`.

The `usb/` subtree is a bounded C++ USB protocol foundation and a read-only
xHCI PCI capability-probe ET_REL skeleton. It intentionally does not provide
USB class drivers or transfer-ring hardware support.
