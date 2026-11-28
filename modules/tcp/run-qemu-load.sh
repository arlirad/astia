#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${TCP_BUILD:-$root/build-tcp}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
log=${TCP_LOG:-${TMPDIR:-/tmp}/astia-tcp-load.log}
qemu=${QEMU:-qemu-system-x86_64}
timeout_seconds=${TCP_TIMEOUT:-30}
staging=${build}/tcp-initrd
initrd=${build}/tcp-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
    cmake --build "$build" --target astiakrn_runtime tcp_module
    kernel=$build/kernel/src/astiakrn_runtime
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime tcp_module
    kernel=$artifact_build/kernel/src/astiakrn_module_runtime
fi

rm -rf "$staging"
mkdir -p "$staging/mod"
cp "$artifact_build/modules/tcp/tcp.km" "$staging/mod/tcp.km"
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
grep -qF 'tcp: ready' "$log"
grep -qF 'mod: mod/tcp.km rc=0' "$log"
