#!/usr/bin/env sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
iso=${1:?usage: test-grub-smp.sh <iso>}
log=${TMPDIR:-/tmp}/astia_grub_smp_$$.log
trap 'rm -f "$log"' EXIT INT TERM

set +e
timeout 10 "$qemu" -machine q35 -smp 2 -cdrom "$iso" \
    -serial "file:$log" -display none -monitor none -no-reboot
status=$?
set -e
cat "$log"

# The production image intentionally parks after boot, so timeout is expected.
grep -qF "smp: discovered=2 online=1" "$log"
grep -qF "online=2" "$log"
if grep -qF "core: boot ok" "$log"; then
    :
else
    grep -qF "core: selftest ok" "$log"
fi
if [ "${ASTIA_GRUB_EXPECT_AP_WORK:-0}" = 1 ]; then
    grep -qF "smp scheduler: AP work ok; stress=8 cpu-mask=2 migration=8 mask=2" "$log"
fi
exit 0
