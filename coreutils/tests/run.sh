#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${COREUTILS_TEST_BUILD:-$root/coreutils/tests/build}
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
kernel_build=$(CDPATH= cd -- "$(dirname -- "$kernel")/.." && pwd)
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
cpio=${ASTIA_CPIO:-cpio}
target=x86_64-unknown-elf
qemu_timeout_seconds=120
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
command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}

rm -rf "$out"
mkdir -p "$out/bin" "$out/root/bin" "$out/root/sbin"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" COREUTILS_BUILD="$out" \
    "$root/coreutils/build.sh"
mkdir -p "$out/host"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/coreutils/src" \
    "$root/coreutils/tests/ls_layout_test.c" -o "$out/host/ls_layout_test"
"$out/host/ls_layout_test"
mkdir -p "$out/root/bin" "$out/root/sbin" "$out/root/etc"

common="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
# shellcheck disable=SC2086
$clang $common -fno-builtin -nostdinc -isystem "$resource/include" \
    -isystem "$sysroot/include" -c "$root/coreutils/tests/coreutils_test.c" \
    -o "$out/coreutils_test.o"
# shellcheck disable=SC2086
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$out/root/sbin/init" \
    "$crt0" "$out/coreutils_test.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
cp "$out/bin/"* "$out/root/bin/"
for key in "$out/.keys/"*; do
    [ -f "$key" ] || continue
    [ "$(basename "$key")" = sources ] && continue
    cp "$key" "$out/root/bin/$(basename "$key").key"
done
cp "$root/daemond/passwd" "$out/root/etc/passwd"
cp "$root/daemond/group" "$out/root/etc/group"
init_libc=$kernel_build/tests/fixtures/init_libc
init_libc=${ASTIA_INIT_LIBC:-$init_libc}
test -f "$init_libc" || {
    echo "Astia init_libc fixture missing: $init_libc" >&2
    exit 1
}
cp "$init_libc" "$out/root/bin/"
# The libc-backed fixtures are dynamically linked: stage the interpreter and
# the shared libc next to them.
ld_so=${ASTIA_LD_SO:-$kernel_build/tests/fixtures/ld.astia.so}
for required in "$sysroot/lib/libc.so" "$ld_so"; do
    test -f "$required" || {
        echo "Astia dynamic runtime missing: $required" >&2
        exit 1
    }
done
mkdir -p "$out/root/lib"
cp "$sysroot/lib/libc.so" "$out/root/lib/libc.so"
cp "$ld_so" "$out/root/lib/ld.astia.so"
(cd "$out/root" && find sbin bin etc lib -print |
    "$cpio" -o --format=newc --quiet) >"$out/initrd.cpio"

log="$out/qemu.log"
set +e
timeout "$qemu_timeout_seconds" "$qemu" -m 128M -kernel "$kernel" -initrd "$out/initrd.cpio" \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e
cat "$log"
test "$status" -eq "$expected_qemu_status"
grep -qF 'coreutils tests ok' "$log"
