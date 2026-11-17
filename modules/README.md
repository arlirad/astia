# Astia loadable modules

Loadable driver sources belong under this directory. Kernel-owned module loading
and the shared module ABI remain under `kernel/src/mod` and
`kernel/include/astia`; module sources include `module_abi.hpp` for typed
descriptors and ownership helpers. `module_abi_core.h` is the narrow C-linkage
boundary for the ET_REL loader entry. Semantic device, driver, packet, and
registry contracts are C++ only. Module sources do not copy loader or device
definitions.

In-tree kernel drivers may use the typed contracts under `astia/dev` directly;
loadable ET_REL modules use `astia/module_abi.hpp` and its typed registration
adapters, which are the stable unloadable-module boundary.

The `tests/` subtree contains small ET_REL fixtures used by the kernel loader
tests. Hardware drivers use one directory per driver, such as `modules/nvme`.

The `usb/` subtree is a bounded C++ USB protocol foundation and a read-only
xHCI PCI capability-probe ET_REL skeleton. It intentionally does not provide
USB class drivers or transfer-ring hardware support.
