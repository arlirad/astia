# Astia coreutils

Small native utilities for Astia.  These are intentionally independent of
GNU coreutils: each program is freestanding, statically linked against the
Astia libc, and keeps its behavior small enough to test on the real kernel.

Currently built:

`basename` `cat` `chmod` `cp` `cut` `daemonctl` `dirname` `echo` `env` `false`
`find` `grep` `head` `id` `ln` `login` `ls` `mkdir` `mv` `poweroff` `ps` `pwd`
`readlink` `reboot` `rm` `sort` `tee` `touch` `true` `uname` `whoami` `wc`

Build them with:

```sh
ASTIA_SYSROOT=/tmp/astia-zsh-build/sysroot \
  utils/coreutils/build.sh /tmp/astia-zsh-build/astia/image/crt0.o
```

The resulting executables are placed in `utils/coreutils/build/bin/`.  The zsh QEMU
launcher builds and installs them into `/bin` automatically.

`env` prints `NAME=VALUE` entries in `environ` order; `env -s` /
`--sorted` prints them byte-lexicographically by the whole entry (C-locale
`strcmp`: `A=10` before `A=2`, uppercase before lowercase). Sorting never
mutates `environ` and never changes the environment of `env COMMAND`.

The utilities deliberately use only native Astia primitives (`read`, `write`,
`open`, `close`, `mkdir`, `unlink`, `rmdir`, `environ`, native power operations
through `astia_power`, process exit, and the libc directory API).  The
QEMU regression suite can be run with `utils/coreutils/tests/run.sh`; it builds the
utilities and checks their exit status and output on the real kernel.

`ls -l` prints native mode, link count, numeric owner IDs, size, timestamp, and
symlink targets. `grep` supports `-n`, `-i`, and `-v` with the usual 0/1/2
match/error statuses; `PATTERN` is a POSIX basic regular expression with
anchors, `.`, `*`, bracket expressions, `\{m,n\}` intervals, `\(...\)` groups,
and `\1`-`\9` back-references. `touch FILE...` creates missing files and
updates the native modification timestamp through the Astia VFS/libc ABI. `-c` skips
creation and `-m` selects that supported modification-time update; the current
timestamp ABI has no access-time or explicit-time operation, so requests using
`-a` are rejected rather than faked.

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

See [`../../docs/userspace/coreutils.md`](../../docs/userspace/coreutils.md)
for the complete command reference.
