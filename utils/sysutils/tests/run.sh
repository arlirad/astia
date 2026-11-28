#!/bin/sh
# Boot the sysutils tools against the module-loaded boot initrd and check the
# list/stat/open/close paths over the serial console.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
build=${SYSUTILS_TEST_BUILD:-$root/utils/sysutils/tests/build}
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_test}
base=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.cpio}
clang=${CLANG:-clang}
cxx=${CXX:-clang++}
qemu=${QEMU:-qemu-system-x86_64}
cpio=${ASTIA_CPIO:-cpio}
target=x86_64-unknown-elf
resource=$($clang --target=$target -print-resource-dir)
builtins=$resource/lib/linux/libclang_rt.builtins-x86_64.a

for required in "$kernel" "$base" "$sysroot/lib/libastia_libc.a"; do
    test -f "$required" || {
        echo "sysutils smoke input missing: $required" >&2
        exit 1
    }
done
test -f "$crt0" || {
    echo "sysutils smoke crt0 missing: $crt0" >&2
    exit 1
}

rm -rf "$build"
mkdir -p "$build/root/bin" "$build/root/sbin" "$build/root/lib" "$build/output"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" SYSUTILS_BUILD="$build/output" \
    "$root/utils/sysutils/build.sh"

common="--target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie \
    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
# shellcheck disable=SC2086
$clang $common -nostdinc -isystem "$resource/include" -isystem "$sysroot/include" \
    -c "$root/utils/sysutils/tests/runner.c" -o "$build/runner.o"
# shellcheck disable=SC2086
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$build/runner" \
    "$crt0" "$build/runner.o" "$sysroot/lib/libastia_libc.a" "$builtins"

$cxx -std=c++20 -c -ffreestanding -fno-pic -fno-pie -mcmodel=large \
    -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -DASTIA_MODULE_ET_REL=1 -I"$root/kernel/include" \
    -o "$build/lifecycle.km" "$root/utils/sysutils/tests/lifecycle_module.cpp"

(cd "$build/root" && "$cpio" -idmu --quiet --no-absolute-filenames) <"$base"
cp "$build/output/bin/"* "$build/root/bin/"
cp "$build/lifecycle.km" "$build/root/lib/sysutils-lifecycle.km"
cp "$build/runner" "$build/root/sbin/init"
(cd "$build/root" && find . -print |
    "$cpio" -o --format=newc --quiet) >"$build/initrd.cpio"

log=$build/qemu.log
set +e
timeout 120 "$qemu" -kernel "$kernel" -initrd "$build/initrd.cpio" \
    -serial "file:$log" \
    -display none \
    -monitor none \
    -device isa-debug-exit \
    -no-reboot
status=$?
set -e

cat "$log"

if [ "$status" -ne 1 ]; then
    echo "sysutils smoke: qemu status $status" >&2
    exit 1
fi

for pattern in "SYSUTILS_OK" "ID STATE FLAGS REFS DEPS SIZE NAME" "name: " \
    "modopen: /missing-module.o:" "runner: lifecycle module token " \
    "runner: lifecycle module closed" \
    "modopen: /lib/sysutils-lifecycle.km: Operation not permitted"; do
    if ! grep -qF "$pattern" "$log"; then
        echo "sysutils smoke: missing output: $pattern" >&2
        exit 1
    fi
done

if ! grep -Eq "modclose: [0-9]+:[0-9]+: Operation not permitted" "$log"; then
    echo "sysutils smoke: missing non-root modclose denial" >&2
    exit 1
fi

echo "sysutils smoke: PASS"
