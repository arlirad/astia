#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${ASTIA_USB_BUILD:-$root/build-usb}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
log=${ASTIA_USB_LOG:-$build/usb-qemu.log}
trace=${ASTIA_USB_TRACE:-$build/usb-qemu.trace}
staging=$build/usb-initrd
initrd=$build/usb-initrd.cpio
qemu=${QEMU:-qemu-system-x86_64}

if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; USB host tests remain the smoke test'
    exit 77
fi
if ! command -v cpio >/dev/null 2>&1; then
    printf '%s\n' 'cpio unavailable; USB host tests remain the smoke test'
    exit 77
fi

cleanup() {
    rm -rf "$staging" "$initrd" "$log" "$trace"
}
trap cleanup EXIT HUP INT TERM

if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
    cmake --build "$build" --target astiakrn_runtime usb_module
    kernel=$build/kernel/src/astiakrn_runtime
else
    cmake --build "$artifact_build" --target astiakrn_module_runtime usb_module
    kernel=$artifact_build/kernel/src/astiakrn_module_runtime
fi
if test "${ASTIA_USB_ROLLBACK_TEST:-0}" = 1; then
    cmake --build "$artifact_build" --target astiakrn_module_runtime "${ASTIA_USB_MODULE_TARGET:-usb_failure_module}"
fi
mkdir -p "$staging"
cp "${ASTIA_USB_MODULE:-$artifact_build/modules/usb/usb.o}" "$staging/usb.o"
(cd "$staging" && find usb.o -print | cpio -o -H newc > "$initrd")

rm -f "$log" "$trace"
set +e
timeout "${ASTIA_USB_TIMEOUT:-30}" "$qemu" \
    -machine q35 -m 128M -kernel "$kernel" \
    -initrd "$initrd" -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot \
    -trace "enable=usb_xhci_*,file=$trace"
status=$?
set -e

cat "$log"
test "$status" -eq 1
if test "${ASTIA_USB_ROLLBACK_TEST:-0}" = 1; then
    grep -qF 'usb-test: rollback-ok' "$log"
    printf '%s\n' 'QEMU xHCI PCI rollback smoke passed'
    exit 0
fi
grep -qF 'mod: usb.o rc=0' "$log"
grep -qF 'usb: xhci started' "$log"
cmd_ring=$(sed -n 's/.*usb: xhci cmd ring=\([0-9][0-9]*\).*/\1/p' "$log" | head -n 1)
crcr=$(sed -n 's/.*usb: xhci crcr=\([0-9][0-9]*\).*/\1/p' "$log" | head -n 1)
slot_trb=$(sed -n 's/.*usb: xhci enable slot trb=\([0-9][0-9]*\).*/\1/p' "$log" |
    head -n 1)
slot=$(sed -n 's/.*usb: xhci enable slot=\([0-9][0-9]*\).*/\1/p' "$log" | head -n 1)
test -n "$cmd_ring" -a -n "$crcr" -a -n "$slot_trb" -a -n "$slot"
test "$crcr" -eq $((cmd_ring + 1))
test "$slot_trb" -eq "$cmd_ring"
test "$slot" -ge 1
crcr_hex=$(printf '%x' "$crcr")
slot_trb_hex=$(printf '%x' "$slot_trb")
grep -q "usb_xhci_oper_write off 0x0018, val 0x0*${crcr_hex}" "$trace"
grep -q "usb_xhci_fetch_trb addr 0x0*${slot_trb_hex}, CR_ENABLE_SLOT" "$trace"
crcr_line=$(grep -n "usb_xhci_oper_write off 0x0018, val 0x0*${crcr_hex}" "$trace" |
    head -n 1 | cut -d: -f1)
fetch_line=$(grep -n "usb_xhci_fetch_trb addr 0x0*${slot_trb_hex}, CR_ENABLE_SLOT" "$trace" |
    head -n 1 | cut -d: -f1)
test -n "$crcr_line" -a -n "$fetch_line"
test "$crcr_line" -lt "$fetch_line"
enable_line=$(awk -v n="$fetch_line" -v s="$slot" \
    'NR > n && index($0, "usb_xhci_slot_enable slotid " s) {print NR; exit}' "$trace")
test -n "$enable_line"
complete_line=$(awk -v n="$enable_line" -v p="$slot_trb_hex" \
    'NR > n && /usb_xhci_queue_event/ && /CC_SUCCESS/ && index($0, p) {print NR; exit}' \
    "$trace")
test -n "$complete_line"
printf '%s\n' 'QEMU xHCI module CRCR/Enable Slot evidence passed (address/HID remain residual)'
