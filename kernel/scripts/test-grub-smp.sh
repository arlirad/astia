#!/usr/bin/env sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
iso=${1:?usage: test-grub-smp.sh <iso>}
smp=${ASTIA_SMP:-2}
qemu_timeout_seconds=10
log=${TMPDIR:-/tmp}/astia_grub_smp_$$.log
trap 'rm -f "$log"' EXIT INT TERM

require_pattern() {
    if ! grep -qF "$1" "$log"; then
        echo "test-grub-smp: missing required output: $1" >&2
        echo "test-grub-smp: last 40 serial log lines:" >&2
        tail -n 40 "$log" >&2
        exit 1
    fi
}

set +e
timeout "$qemu_timeout_seconds" "$qemu" -machine q35 -smp "$smp" -cdrom "$iso" \
    -serial "file:$log" -display none -monitor none -no-reboot
status=$?
set -e
cat "$log"

# The production image intentionally parks after boot, so timeout is expected.
require_pattern "smp: discovered=$smp online=1"
require_pattern "online=$smp"
if [ "${ASTIA_GRUB_EXPECT_FRAMEBUFFER:-0}" = 1 ]; then
    require_pattern "framebuffer: ready"
elif [ "${ASTIA_GRUB_EXPECT_FRAMEBUFFER:-0}" = unavailable ]; then
    require_pattern "framebuffer: unavailable"
fi
if grep -qF "core: boot ok" "$log"; then
    :
else
    require_pattern "core: selftest ok"
fi
if [ "${ASTIA_GRUB_EXPECT_AP_WORK:-0}" = 1 ]; then
    require_pattern "smp scheduler: AP work ok"
    require_pattern "smp scheduler: migration ok"
fi
if [ "${ASTIA_GRUB_EXPECT_APIC_TOPOLOGY:-0}" = 1 ]; then
    if grep -qF "AP startup deferred" "$log"; then
        echo "test-grub-smp: AP startup was deferred" >&2
        exit 1
    fi
fi
exit 0
