# Astia coreutils

Small native utilities for Astia.  These are intentionally independent of
GNU coreutils: each program is freestanding, statically linked against the
Astia libc, and keeps its behavior small enough to test on the real kernel.

Currently built:

`cat` `cp` `cut` `echo` `env` `false` `grep` `head` `ls` `mkdir` `mv` `pwd`
`rm` `sort` `tee` `true` `uname` `wc`

Build them with:

```sh
ASTIA_SYSROOT=/tmp/astia-zsh-build/sysroot \
  coreutils/build.sh /tmp/astia-zsh-build/astia/image/crt0.o
```

The resulting executables are placed in `coreutils/build/bin/`.  The zsh QEMU
launcher builds and installs them into `/bin` automatically.

The utilities deliberately use only native Astia primitives (`read`, `write`,
`open`, `close`, `mkdir`, `unlink`, `rmdir`, `environ`, process exit, and the
libc directory API).  The
QEMU regression suite can be run with `coreutils/tests/run.sh`; it builds the
utilities and checks their exit status and output on the real kernel.
