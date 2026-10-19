#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_build=${ZSH_BUILD:-/tmp/astia-zsh-build}
zsh_source=${ZSH_SOURCE:-$zsh_build/source}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.tar}
work=${ASTIA_ZSH_RUN:-/tmp/astia-zsh-run}
clang=${CLANG:-clang}
target=x86_64-unknown-elf
resource_dir=$($clang --target=$target -print-resource-dir)
sysroot=$zsh_build/sysroot

if [ ! -t 0 ]; then
    echo "error: interactive zsh needs a real terminal; stdin is already EOF" >&2
    echo "run this script directly from a terminal, not through a closed pipe" >&2
    exit 2
fi

if [ ! -d "$zsh_source" ]; then
    if [ -n "${ZSH_SOURCE:-}" ]; then
        echo "ZSH_SOURCE does not exist: $zsh_source" >&2
        exit 1
    fi
    mkdir -p "$(dirname -- "$zsh_source")"
    git clone --depth=1 https://github.com/zsh-users/zsh.git "$zsh_source"
fi

ZSH_SOURCE="$zsh_source" ZSH_BUILD="$zsh_build" "$root/toolchain/build-zsh.sh"

rm -rf "$work"
mkdir -p "$work/root/sbin" "$work/root/bin"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/coreutils/build.sh"

tar xf "$base_initrd" -C "$work/root"

common_flags="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
include_flags="-nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

# shellcheck disable=SC2086
$clang $common_flags -fno-builtin $include_flags \
    -c -o "$work/runner.o" "$root/toolchain/zsh-runner.c"
# shellcheck disable=SC2086
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/runner" "$zsh_build/astia/image/crt0.o" "$work/runner.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"

cp "$work/runner" "$work/root/sbin/init"
cp "$zsh_build/output/zsh" "$work/root/bin/zsh"
cp "$work/coreutils/bin/"* "$work/root/bin/"
tar cf "$work/initrd.tar" -C "$work/root" hello_initrd.o sbin/init bin

echo "Starting interactive Astia zsh; exit with Ctrl-D or 'exit'." >&2
exec qemu-system-x86_64 -m 128M -kernel "$kernel" -initrd "$work/initrd.tar" \
    -serial stdio -display none -monitor none -device isa-debug-exit -no-reboot
