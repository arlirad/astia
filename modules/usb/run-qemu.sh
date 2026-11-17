#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${ASTIA_USB_BUILD:-$root/build-usb}
log=${ASTIA_USB_LOG:-$build/usb-qemu.log}
trace=${ASTIA_USB_TRACE:-$build/usb-qemu.trace}
staging=$build/usb-initrd
initrd=$build/usb-initrd.cpio

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; USB host tests remain the smoke test'
    exit 0
fi
if ! command -v cpio >/dev/null 2>&1; then
    printf '%s\n' 'cpio unavailable; USB host tests remain the smoke test'
    exit 0
fi

cleanup() {
    rm -rf "$staging" "$initrd" "$log" "$trace"
}
trap cleanup EXIT HUP INT TERM

cmake -S "$root" -B "$build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
    -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON
cmake --build "$build" --target astiakrn_runtime usb_module
mkdir -p "$staging"
cp "$build/modules/usb/usb.o" "$staging/usb.o"
(cd "$staging" && find usb.o -print | cpio -o -H newc > "$initrd")

set +e
timeout "${ASTIA_USB_TIMEOUT:-30}" qemu-system-x86_64 \
    -machine q35 -m 128M -kernel "$build/kernel/src/astiakrn_runtime" \
    -initrd "$initrd" -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot \
    -trace "enable=usb_xhci_*,file=$trace"
status=$?
set -e

cat "$log"
test "$status" -eq 1
grep -qF 'mod: usb.o rc=0' "$log"
grep -qF 'usb_xhci_slot_enable slotid 1' "$trace"
grep -qF 'usb_xhci_slot_address slotid 1, port 1' "$trace"
grep -qF 'usb_xhci_slot_configure slotid 1' "$trace"
grep -qF 'TR_SETUP, p 0x0008000001000680' "$trace"
grep -qF 'TR_SETUP, p 0x0009000002000680' "$trace"
grep -qF 'TR_SETUP, p 0x0022000002000680' "$trace"
grep -qF 'TR_SETUP, p 0x0000000000010900' "$trace"
printf '%s\n' 'QEMU xHCI module load, address, configuration, and interface enumeration smoke passed'
