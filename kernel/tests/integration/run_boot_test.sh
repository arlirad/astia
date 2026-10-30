#!/bin/sh
# Boot the kernel under QEMU's isa-debug-exit device and translate QEMU's exit
# status into an ordinary pass/fail for CTest.
#
#   usage: run_boot_test.sh <qemu-system-x86_64> <kernel.elf> [initrd] [pattern...]
#
# When <initrd> is given it is passed as -initrd, which QEMU's PVH direct boot
# path places in the boot module list. Every pattern given must appear in the
# serial log for the run to count as a pass.
#
# The kernel writes 0 to port 0x501 when its self-tests pass and 1 when they
# fail. isa-debug-exit maps a written value v to exit status (v << 1) | 1, so a
# successful boot ends with status 1 and a failing one with status 3.

set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 QEMU KERNEL [INITRD] [PATTERN...]" >&2
    exit 2
fi

qemu=$1
kernel=$2
initrd=$3
shift 3    # everything left in "$@" is a required log pattern

log=${TMPDIR:-/tmp}/astia_boot_$$.log
trap 'rm -f "$log"' EXIT INT TERM

smp_args=
if [ -n "${ASTIA_QEMU_SMP:-}" ]; then
    smp_args="-smp $ASTIA_QEMU_SMP"
fi
machine_args=
if [ -n "${ASTIA_QEMU_MACHINE:-}" ]; then
    machine_args="-machine $ASTIA_QEMU_MACHINE"
fi

# QEMU deliberately exits nonzero through isa-debug-exit, so capture that
# status explicitly instead of letting `set -e` hide the diagnostic log.
set +e

# The invocation is written out per branch rather than assembled with
# `set --`, because "$@" carries the patterns across this point.
if [ -n "$initrd" ]; then
    timeout 30 "$qemu" $machine_args $smp_args -kernel "$kernel" -initrd "$initrd" \
        -serial "file:$log" \
        -display none \
        -monitor none \
        -device isa-debug-exit \
        -no-reboot
else
    timeout 30 "$qemu" $machine_args $smp_args -kernel "$kernel" \
        -serial "file:$log" \
        -display none \
        -monitor none \
        -device isa-debug-exit \
        -no-reboot
fi
status=$?
set -e

cat "$log"

if [ "$status" -eq 1 ] && grep -q "core: selftest ok" "$log"; then
    for pattern in "$@"; do
        if ! grep -qF "$pattern" "$log"; then
            echo "run_boot_test: expected output not found: $pattern" >&2
            echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
            exit 1
        fi
    done
    exit 0
fi

echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
exit 1
