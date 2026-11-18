#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${RTL8139_BUILD:-$root/build-rtl8139}
artifact_build=${ASTIA_TEST_BUILD:-$build}
log=${RTL8139_LOG:-${TMPDIR:-/tmp}/astia-rtl8139-load.log}
qemu=${QEMU:-qemu-system-x86_64}
timeout_seconds=${RTL8139_TIMEOUT:-30}
staging=${build}/rtl8139-initrd
initrd=${build}/rtl8139-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime rtl8139_module
fi
rm -rf "$staging"
mkdir -p "$staging"
cp "$artifact_build/modules/rtl8139/rtl8139.o" "$staging/rtl8139.o"
(cd "$staging" && find rtl8139.o -print | cpio -o -H newc > "$initrd")

set +e
timeout "$timeout_seconds" "$qemu" \
    -machine q35 -m 128M -kernel "$artifact_build/kernel/src/astiakrn_module_runtime" \
    -initrd "$initrd" -device rtl8139,netdev=rtltest \
    -netdev user,id=rtltest -serial "file:$log" -display none -monitor none \
    -device isa-debug-exit -no-reboot
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'rtl8139: probe ok' "$log"
grep -qF 'rtl8139: public tx selftest ok' "$log"
