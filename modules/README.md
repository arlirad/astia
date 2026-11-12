# Astia loadable modules

Loadable driver sources belong under this directory. Kernel-owned module loading
and the shared module ABI remain under `kernel/src/mod` and
`kernel/include/astia`; module sources include only those public headers and do
not copy loader or device definitions.

The `tests/` subtree contains small ET_REL fixtures used by the kernel loader
tests. Hardware drivers use one directory per driver, such as `modules/nvme`.
