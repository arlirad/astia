#!/usr/bin/env sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
iso=${1:?usage: test-grub-smp.sh <iso>}
smp=${ASTIA_SMP:-2}
log=${TMPDIR:-/tmp}/astia_grub_smp_$$.log
trap 'rm -f "$log"' EXIT INT TERM

set +e
timeout 10 "$qemu" -machine q35 -smp "$smp" -cdrom "$iso" \
    -serial "file:$log" -display none -monitor none -no-reboot
status=$?
set -e
cat "$log"

# The production image intentionally parks after boot, so timeout is expected.
grep -qF "smp: discovered=$smp online=1" "$log"
grep -qF "online=$smp" "$log"
if grep -qF "core: boot ok" "$log"; then
    :
else
    grep -qF "core: selftest ok" "$log"
fi
if [ "${ASTIA_GRUB_EXPECT_AP_WORK:-0}" = 1 ]; then
    grep -qF "smp scheduler: AP work ok" "$log"
    grep -qF "smp scheduler: migration ok" "$log"
fi
exit 0
