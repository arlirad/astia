#!/bin/sh
set -eu

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; RTL8139 protocol unit is the smoke test'
    exit 0
fi

set +e
timeout 2 qemu-system-x86_64 -machine q35 -nodefaults -device rtl8139,netdev=rtltest \
    -netdev user,id=rtltest -display none -serial none -monitor none -S >/dev/null 2>&1
result=$?
set -e

if [ "$result" -ne 124 ]; then
    printf '%s\n' 'QEMU RTL8139 device failed to remain available for probe smoke' >&2
    exit 1
fi
printf '%s\n' 'QEMU RTL8139 device creation smoke passed; stack RX/TX remains unimplemented'
