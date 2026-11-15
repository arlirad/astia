# Astia loadable modules

Loadable driver sources belong under this directory. Kernel-owned module loading
and the shared module ABI remain under `kernel/src/mod` and
`kernel/include/astia`; C++ module sources include `module_abi.hpp` for typed
descriptors and ownership helpers. The `.h` headers remain a freestanding
compatibility shim for the ET_REL loader and foreign modules. Module sources
do not copy loader or device definitions.

The `tests/` subtree contains small ET_REL fixtures used by the kernel loader
tests. Hardware drivers use one directory per driver, such as `modules/nvme`.

The `usb/` subtree is a bounded C++ USB protocol foundation and a read-only
xHCI PCI capability-probe ET_REL skeleton. It intentionally does not provide
USB class drivers or transfer-ring hardware support.
