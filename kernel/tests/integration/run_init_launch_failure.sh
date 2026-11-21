#!/bin/sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
kernel=${ASTIA_KERNEL:-}
if [ -z "$kernel" ]; then
    echo "ASTIA_KERNEL must name the production kernel ELF" >&2
    exit 2
fi
log=${TMPDIR:-/tmp}/astia-init-launch-$$.log
trap 'rm -f "$log"' EXIT INT TERM

set +e
timeout 5 "$qemu" -kernel "$kernel" \
    -append '--init /sbin/does-not-exist' \
    -serial "file:$log" -display none -monitor none -no-reboot
status=$?
set -e

cat "$log"
if ! grep -qF 'unable to launch init path=/sbin/does-not-exist' "$log" ||
   ! grep -qF 'PANIC' "$log"; then
    echo "init launch failure was not reported and panicked" >&2
    exit 1
fi
if [ "$status" -ne 124 ]; then
    echo "expected kernel panic to hold the machine, QEMU status was $status" >&2
    exit 1
fi
