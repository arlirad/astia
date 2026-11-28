#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${EXT4_BUILD:-$root/build-ext4}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
log=${EXT4_LOG:-${TMPDIR:-/tmp}/astia-ext4-load.log}
qemu=${QEMU:-qemu-system-x86_64}
timeout_seconds=${EXT4_TIMEOUT:-30}
staging=${build}/ext4-initrd
initrd=${build}/ext4-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
    cmake --build "$build" --target astiakrn_module_runtime ext4_module
    kernel=$build/kernel/src/astiakrn_module_runtime
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime ext4_module
    kernel=$artifact_build/kernel/src/astiakrn_module_runtime
fi

rm -rf "$staging"
mkdir -p "$staging/mod"
cp "$artifact_build/modules/ext4/ext4.km" "$staging/mod/ext4.km"
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
grep -qF 'ext4: ready' "$log"
grep -qF 'mod: mod/ext4.km rc=0' "$log"
