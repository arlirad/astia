#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${VIRTIO_BLK_BUILD:-$root/build-virtio-blk}
artifact_build=${ASTIA_TEST_BUILD:-$build}
image=${VIRTIO_BLK_IMAGE:-$root/modules/virtio-blk/fixture.raw}
timeout_seconds=${VIRTIO_BLK_TIMEOUT:-40}
qemu=${QEMU:-qemu-system-x86_64}
log=${VIRTIO_BLK_LOG:-${TMPDIR:-/tmp}/astia-virtio-blk.log}
readonly=${VIRTIO_BLK_READONLY:-0}
staging=${build}/virtio-blk-initrd
initrd=${build}/virtio-blk-initrd.cpio

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime virtio_blk_module
fi
rm -rf "$staging"
mkdir -p "$staging"
cp "$artifact_build/modules/virtio-blk/virtio_blk.o" "$staging/virtio_blk.o"
(cd "$staging" && find virtio_blk.o -print | cpio -o -H newc > "$initrd")

drive_readonly=
if test "$readonly" = 1; then
    drive_readonly=,readonly=on
fi

set +e
timeout "$timeout_seconds" "$qemu" \
    -machine q35 -m 128M -kernel "$artifact_build/kernel/src/astiakrn_module_runtime" \
    -initrd "$initrd" \
    -drive "if=none,format=raw,file=$image,id=vblk0$drive_readonly" \
    -device virtio-blk-pci,drive=vblk0,disable-legacy=on \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'vblk: sectors=' "$log"
grep -qF 'vblk: known sector ok' "$log"
grep -qF 'vblk: invalid geometry rejected' "$log"
if test "$readonly" = 1; then
    grep -qF 'vblk: write rejected: read-only or no flush' "$log"
else
    grep -qF 'vblk: write flush persistence ok' "$log"
fi
