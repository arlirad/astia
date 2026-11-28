# Native utilities

- `coreutils/`: portable userspace utilities built against the Astia libc.
- `sysutils/`: Astia-specific module tools (`modls`, `modstat`, `modopen`,
  `modclose`).

Each directory keeps its own build script, README, and QEMU regression suite.
The documented module lifecycle commands are described in
[`docs/userspace`](../docs/userspace/).
