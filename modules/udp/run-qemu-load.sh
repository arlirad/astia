#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${UDP_BUILD:-$root/build-udp}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
log=${UDP_LOG:-${TMPDIR:-/tmp}/astia-udp-load.log}
qemu=${QEMU:-qemu-system-x86_64}
timeout_seconds=${UDP_TIMEOUT:-30}
staging=${build}/udp-initrd
initrd=${build}/udp-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
    cmake --build "$build" --target astiakrn_runtime udp_module
    kernel=$build/kernel/src/astiakrn_runtime
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime udp_module
    kernel=$artifact_build/kernel/src/astiakrn_module_runtime
fi

rm -rf "$staging"
mkdir -p "$staging/mod"
cp "$artifact_build/modules/udp/udp.km" "$staging/mod/udp.km"
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
grep -qF 'udp: ready' "$log"
grep -qF 'mod: mod/udp.km rc=0' "$log"
