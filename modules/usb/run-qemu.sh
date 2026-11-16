#!/bin/sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; capability unit test is the smoke test'
    exit 77
fi

set +e
timeout 2 "$qemu" -machine q35 -nodefaults -device qemu-xhci,id=xhci \
    -display none -serial none -monitor none -S >/dev/null 2>&1
result=$?
set -e

if [ "$result" -ne 124 ]; then
    printf '%s\n' 'QEMU xHCI device failed to remain available for capability smoke' >&2
    exit 1
fi
printf '%s\n' 'QEMU xHCI device creation smoke passed; register access remains unsupported'
