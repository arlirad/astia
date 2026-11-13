# Astia coreutils

Small native utilities for Astia.  These are intentionally independent of
GNU coreutils: each program is freestanding, statically linked against the
Astia libc, and keeps its behavior small enough to test on the real kernel.

Currently built:

`basename` `cat` `chmod` `cp` `cut` `dirname` `echo` `env` `false` `find` `grep`
`head` `id` `ln` `login` `ls` `mkdir` `mv` `pwd` `readlink` `rm` `sort` `tee`
`touch` `true` `uname` `whoami` `wc`

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

`ls -l` prints native mode, link count, numeric owner IDs, size, timestamp, and
symlink targets. `grep` supports `-n`, `-i`, and `-v` with the usual 0/1/2
match/error statuses. `touch FILE...` creates missing files and updates
timestamps through the native Astia VFS/libc ABI. `-a` and `-m` are accepted
for compatibility, but the current timestamp ABI updates the file timestamp
as a whole; selective access/modify timestamp updates are not yet available.

`cp -r`, `rm -r`, and the native `find PATH...` traversal use the directory API.
`ln` and `readlink` use the native symbolic-link ABI.
`chmod` supports octal and POSIX symbolic modes, including recursive `-R` traversal;
`basename` and `dirname` implement the POSIX pathname transformations.

`login` is a minimal local session launcher. It reads a username from its
controlling terminal, uses `/etc/passwd` and `/etc/group`, installs native
credentials and supplementary groups, and execs the account shell. There is
no PAM, shadow, crypt, networking, or password backend: only the `x`
convention (and an empty, explicitly passwordless field) is accepted; other
password fields are rejected clearly.
