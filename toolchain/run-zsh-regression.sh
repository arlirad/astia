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
cpio=${ASTIA_CPIO:-cpio}

command -v "$qemu" >/dev/null 2>&1 || {
    echo "QEMU executable not found: $qemu" >&2
    exit 1
}
command -v timeout >/dev/null 2>&1 || {
    echo "timeout command not found (required for the regression test)" >&2
    exit 1
}
command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}
sysroot=$build/sysroot
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$root/build-tests/tests/fixtures/boot_initrd.cpio}

test -x "$build/output/zsh"
rm -rf "$work"
mkdir -p "$work/root/sbin" "$work/root/bin"
(cd "$work/root" && "$cpio" -id --quiet --no-absolute-filenames) <"$base_initrd"
cp "$build/output/zsh" "$work/root/bin/zsh"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/utils/coreutils/build.sh"
cp "$work/coreutils/bin/"* "$work/root/bin/"

$clang --target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie \
    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
    -nostdinc -isystem "$resource/include" -isystem "$sysroot/include" \
    -c "$root/toolchain/zsh-regression-runner.c" -o "$work/runner.o"
$clang --target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
    -fno-asynchronous-unwind-tables -mno-red-zone -nostdlib -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$work/root/sbin/init" \
    "$build/astia/image/crt0.o" "$work/runner.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
(cd "$work/root" && find sbin bin -print |
    "$cpio" -o --format=newc --quiet) >"$work/initrd.cpio"

log=$work/qemu.log
set +e
timeout "$qemu_timeout_seconds" "$qemu" -m 128M -kernel "$kernel" -initrd "$work/initrd.cpio" \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e
cat "$log"
test "$status" -eq "$expected_qemu_status"
grep -q 'astia-signal-ok' "$log"
