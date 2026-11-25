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
# CTest grants this test a 60s budget.  Keep the internal deadline strictly
# below it so a stalled guest still gets its serial log printed instead of
# being SIGKILLed mid-report.  The required markers and exit status are
# unchanged.
#
# Only one-to-three digit overrides are accepted.  `timeout` takes 64-bit
# values, and shell arithmetic is not a portable way to detect that an
# override overflowed the machine word, so the width is rejected before any
# comparison.
boot_timeout_seconds() {
    seconds=${ASTIA_BOOT_QEMU_TIMEOUT:-45}
    case "$seconds" in
        '' | *[!0-9]*) seconds=45 ;;
    esac
    if [ "${#seconds}" -gt 3 ]; then
        seconds=45
    elif [ "$seconds" -lt 1 ]; then
        seconds=1
    elif [ "$seconds" -gt 55 ]; then
        seconds=55
    fi
    printf '%s\n' "$seconds"
}

qemu_timeout_seconds=$(boot_timeout_seconds)
expected_qemu_status=${ASTIA_EXPECT_QEMU_STATUS:-1}
require_selftest=${ASTIA_REQUIRE_SELFTEST:-1}
forbidden_pattern=${ASTIA_FORBID_PATTERN:-}
once_pattern=${ASTIA_EXPECT_ONCE_PATTERN:-}
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
    timeout "$qemu_timeout_seconds" "$qemu" $machine_args $smp_args -kernel "$kernel" -initrd "$initrd" \
        -serial "file:$log" \
        -display none \
        -monitor none \
        -device isa-debug-exit \
        -no-reboot
else
    timeout "$qemu_timeout_seconds" "$qemu" $machine_args $smp_args -kernel "$kernel" \
        -serial "file:$log" \
        -display none \
        -monitor none \
        -device isa-debug-exit \
        -no-reboot
fi
status=$?
set -e

if [ "$status" -eq 124 ]; then
    echo "run_boot_test: qemu exceeded ${qemu_timeout_seconds}s internal timeout" >&2
    cat "$log"
    echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
    exit 1
fi

cat "$log"

if [ "$status" -eq "$expected_qemu_status" ] && {
    [ "$require_selftest" -eq 0 ] || grep -q "core: selftest ok" "$log"
}; then
    for pattern in "$@"; do
        if ! grep -qF "$pattern" "$log"; then
            echo "run_boot_test: expected output not found: $pattern" >&2
            echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
            exit 1
        fi
    done
    if [ -n "$once_pattern" ]; then
        count=$(grep -cF "$once_pattern" "$log" || true)
        if [ "$count" -ne 1 ]; then
            echo "run_boot_test: expected exactly one occurrence of: $once_pattern (found $count)" >&2
            echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
            exit 1
        fi
    fi
    if [ -n "$forbidden_pattern" ] && grep -qF "$forbidden_pattern" "$log"; then
        echo "run_boot_test: forbidden output found: $forbidden_pattern" >&2
        echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
        exit 1
    fi
    exit 0
fi

echo "run_boot_test: kernel boot test failed (qemu status $status)" >&2
exit 1
