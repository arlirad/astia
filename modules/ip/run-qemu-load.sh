#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${IP_BUILD:-$root/build-ip}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
log=${IP_LOG:-${TMPDIR:-/tmp}/astia-ip-load.log}
qemu=${QEMU:-qemu-system-x86_64}
timeout_seconds=${IP_TIMEOUT:-30}
staging=${build}/ip-initrd
initrd=${build}/ip-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
    cmake --build "$build" --target astiakrn_runtime ip_module
    kernel=$build/kernel/src/astiakrn_runtime
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime ip_module
    kernel=$artifact_build/kernel/src/astiakrn_module_runtime
fi

rm -rf "$staging"
mkdir -p "$staging"
mkdir -p "$staging/mod"
cp "$artifact_build/modules/ip/ip.o" "$staging/mod/ip.o"
(cd "$staging" && find mod -print | cpio -o -H newc > "$initrd")

set +e
timeout "$timeout_seconds" "$qemu" \
    -machine q35 -m 128M -kernel "$kernel" \
    -initrd "$initrd" -serial "file:$log" -display none -monitor none \
    -device isa-debug-exit -no-reboot
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'ip: ready' "$log"
grep -qF 'mod: mod/ip.o rc=0' "$log"
