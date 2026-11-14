#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${VIRTIO_BLK_BUILD:-$root/build-virtio-blk}
image=${VIRTIO_BLK_IMAGE:-$root/modules/virtio-blk/fixture.raw}
timeout_seconds=${VIRTIO_BLK_TIMEOUT:-40}
log=${VIRTIO_BLK_LOG:-${TMPDIR:-/tmp}/astia-virtio-blk.log}
staging=${build}/virtio-blk-initrd
initrd=${build}/virtio-blk-initrd.cpio

cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
    -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
cmake --build "$build" --target astiakrn_runtime virtio_blk_module
rm -rf "$staging"
mkdir -p "$staging"
cp "$build/modules/virtio-blk/virtio_blk.o" "$staging/virtio_blk.o"
(cd "$staging" && find virtio_blk.o -print | cpio -o -H newc > "$initrd")

set +e
timeout "$timeout_seconds" qemu-system-x86_64 \
    -machine q35 -m 128M -kernel "$build/kernel/src/astiakrn_runtime" \
    -initrd "$initrd" \
    -drive "if=none,format=raw,file=$image,id=vblk0" \
    -device virtio-blk-pci,drive=vblk0,disable-legacy=on \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'vblk: sectors=' "$log"
grep -qF 'vblk: known sector ok' "$log"
grep -qF 'vblk: invalid geometry rejected' "$log"
grep -qF 'vblk: write rejected: read-only' "$log"
