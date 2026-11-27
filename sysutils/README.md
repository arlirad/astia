# sysutils

Astia-specific userspace tools that wrap the kernel module-management
syscalls. They are intentionally separate from `coreutils`, which stays a
portable POSIX-style command set.

| Tool | Syscall | Purpose |
| --- | --- | --- |
| `modls` | `modls` | List published modules with their `slot:generation` token. |
| `modstat` | `modstat` | Show one module snapshot, including dependencies. |
| `modopen` | `modopen` | Load a kernel module image and print its token. |
| `modclose` | `modclose` | Quiesce and unload a module. |

`modstat` and `modclose` accept either a module name or the stable
`slot:generation` token printed by `modls`/`modopen`. All four tools require
effective UID 0, matching the syscall contract; a non-root call reports
`Operation not permitted`.

Build and stage:

```sh
ASTIA_SYSROOT=<sysroot> ASTIA_CRT0=<crt0.o> sysutils/build.sh
```

The developer initrd build (`toolchain/run-zsh-qemu.sh`) runs this script and
installs the binaries in `/bin`, next to the coreutils and zsh.
