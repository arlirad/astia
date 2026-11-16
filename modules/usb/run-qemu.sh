#!/bin/sh
set -eu

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; capability unit test is the smoke test'
    exit 0
fi

set +e
timeout 2 qemu-system-x86_64 -machine q35 -nodefaults -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -display none -serial none -monitor none -S >/dev/null 2>&1
result=$?
set -e

if [ "$result" -ne 124 ]; then
    printf '%s\n' 'QEMU xHCI device failed to remain available for capability smoke' >&2
    exit 1
fi
printf '%s\n' 'QEMU xHCI plus USB keyboard topology smoke passed; enumeration requires the loaded module PCI path'
