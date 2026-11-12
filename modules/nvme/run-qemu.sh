#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${NVME_BUILD:-$root/build-nvme}
image=${NVME_IMAGE:-$root/modules/nvme/fixture.raw}
log=${NVME_LOG:-${TMPDIR:-/tmp}/astia-nvme-qemu.log}

cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build "$build" --target astiakrn_runtime

set +e
timeout "${NVME_TIMEOUT:-30}" qemu-system-x86_64 \
    -machine q35 -m 128M -kernel "$build/kernel/src/astiakrn_runtime" \
    -drive "if=none,format=raw,file=$image,id=nvme0" \
    -device nvme,drive=nvme0,serial=ASTIA-NVME \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'nvme: namespace blocks=' "$log"
grep -qF 'nvme: known sector ok' "$log"
