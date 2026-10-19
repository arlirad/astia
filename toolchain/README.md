# Astia Clang userspace toolchain

The toolchain file targets `x86_64-unknown-elf` and deliberately excludes the
host C library.  Clang's resource headers provide only compiler headers
(`stddef.h`, `stdint.h`, `stdarg.h`, ...); libc and the ABI headers come from
Astia.

## Build the libc sysroot

```sh
cmake -S libc -B /tmp/astia-clang-libc \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/astia-x86_64-clang.cmake \
  -DASTIA_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/astia-sysroot
cmake --build /tmp/astia-clang-libc
cmake --install /tmp/astia-clang-libc
```

The resulting sysroot contains:

- `include/` — Astia libc headers,
- `include/astia/api/` — kernel/userspace ABI headers,
- `lib/libastia_libc.a` — the freestanding C library.

The first userspace link still needs Astia's `crt0.S` and Clang's compiler
builtins archive.  This is intentional: startup code is an ABI component and
will become part of the userspace runtime before porting zsh.

The current end-to-end smoke test automates the sysroot build and links the
existing libc integration image:

```sh
toolchain/build-init-libc.sh
```

## Build zsh

With a zsh checkout outside this repository:

```sh
ZSH_SOURCE=/tmp/zsh ZSH_BUILD=/tmp/astia-zsh-build \
  toolchain/build-zsh.sh
```

The ELF is written to `/tmp/astia-zsh-build/output/zsh`.  To add it to an
existing initrd:

```sh
toolchain/package-zsh-initrd.sh \
  kernel/build-tests/tests/fixtures/boot_initrd.tar \
  /tmp/astia-zsh-build/output/zsh \
  /tmp/astia-zsh-initrd.tar
```

For an interactive serial session:

```sh
ZSH_SOURCE=/tmp/zsh toolchain/run-zsh-qemu.sh
```

Run it directly from a terminal.  Piping an already-closed stdin into QEMU
is treated as EOF by the shell, so the interactive session exits immediately.

The launcher also builds the native Astia coreutils from `coreutils/` and
places them in `/bin` (`cat`, `echo`, `env`, `false`, `head`, `pwd`, `true`,
`uname`, and `wc`).

For configure-based software, use the target as the host and do not let the
build system search the machine's native headers or libraries:

```sh
./configure --host=x86_64-unknown-elf \
  CC='clang --target=x86_64-unknown-elf' \
  CFLAGS='--sysroot=/tmp/astia-sysroot -ffreestanding -fno-builtin -mno-red-zone' \
  LDFLAGS='--sysroot=/tmp/astia-sysroot -nostdlib -static'
```
