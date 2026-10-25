#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${COREUTILS_TEST_BUILD:-$root/coreutils/tests/build}
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=x86_64-unknown-elf
resource=$($clang --target=$target -print-resource-dir)

rm -rf "$out"
mkdir -p "$out/bin" "$out/root/bin" "$out/root/sbin"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" COREUTILS_BUILD="$out" \
    "$root/coreutils/build.sh"
mkdir -p "$out/root/bin" "$out/root/sbin"

common="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
# shellcheck disable=SC2086
$clang $common -fno-builtin -nostdinc -isystem "$resource/include" \
    -isystem "$sysroot/include" -c "$root/coreutils/tests/coreutils_test.c" \
    -o "$out/coreutils_test.o"
# shellcheck disable=SC2086
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$out/root/sbin/init" \
    "$crt0" "$out/coreutils_test.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
cp "$out/bin/"* "$out/root/bin/"
if [ -f "$root/kernel/build-tests/tests/fixtures/init_libc" ]; then
    cp "$root/kernel/build-tests/tests/fixtures/init_libc" "$out/root/bin/"
fi
tar cf "$out/initrd.tar" -C "$out/root" sbin bin

log="$out/qemu.log"
set +e
timeout 60 "$qemu" -m 128M -kernel "$kernel" -initrd "$out/initrd.tar" \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e
cat "$log"
test "$status" -eq 1
grep -qF 'coreutils tests ok' "$log"
