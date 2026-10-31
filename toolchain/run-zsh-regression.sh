#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${ZSH_BUILD:-/tmp/astia-zsh-build}
work=${ASTIA_ZSH_REGRESSION:-/tmp/astia-zsh-regression}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=x86_64-unknown-elf
qemu_timeout_seconds=60
expected_qemu_status=1
resource=$($clang --target=$target -print-resource-dir)

command -v "$qemu" >/dev/null 2>&1 || {
    echo "QEMU executable not found: $qemu" >&2
    exit 1
}
command -v timeout >/dev/null 2>&1 || {
    echo "timeout command not found (required for the regression test)" >&2
    exit 1
}
sysroot=$build/sysroot
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$root/build-tests/tests/fixtures/boot_initrd.tar}

test -x "$build/output/zsh"
rm -rf "$work"
mkdir -p "$work/root/sbin" "$work/root/bin"
tar xf "$base_initrd" -C "$work/root"
cp "$build/output/zsh" "$work/root/bin/zsh"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/coreutils/build.sh"
cp "$work/coreutils/bin/"* "$work/root/bin/"

$clang --target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie \
    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
    -nostdinc -isystem "$resource/include" -isystem "$sysroot/include" \
    -c "$root/toolchain/zsh-regression-runner.c" -o "$work/runner.o"
$clang --target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
    -fno-asynchronous-unwind-tables -mno-red-zone -nostdlib -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$work/root/sbin/init" \
    "$build/astia/image/crt0.o" "$work/runner.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
tar cf "$work/initrd.tar" -C "$work/root" sbin bin

log=$work/qemu.log
set +e
timeout "$qemu_timeout_seconds" qemu-system-x86_64 -m 128M -kernel "$kernel" -initrd "$work/initrd.tar" \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e
cat "$log"
test "$status" -eq "$expected_qemu_status"
grep -q 'astia-signal-ok' "$log"
