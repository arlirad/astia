# Astia coreutils

Small native utilities for Astia.  These are intentionally independent of
GNU coreutils: each program is freestanding, statically linked against the
Astia libc, and keeps its behavior small enough to test on the real kernel.

Currently built:

`cat` `echo` `env` `false` `head` `ls` `pwd` `true` `uname` `wc`

Build them with:

```sh
ASTIA_SYSROOT=/tmp/astia-zsh-build/sysroot \
  coreutils/build.sh /tmp/astia-zsh-build/astia/image/crt0.o
```

The resulting executables are placed in `coreutils/build/bin/`.  The zsh QEMU
launcher builds and installs them into `/bin` automatically.

The first batch deliberately uses only working Astia primitives (`read`,
`write`, `open`, `close`, `environ`, and process exit).  Directory traversal
will land with `ls` once the libc directory API is backed by the kernel.
