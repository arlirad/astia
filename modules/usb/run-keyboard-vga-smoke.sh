#!/bin/sh
set -eu

qemu=${QEMU:-qemu-system-x86_64}
iso=${ASTIA_USB_VGA_ISO:-}

if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' 'qemu-system-x86_64 unavailable; USB unit test is the smoke test'
    exit 0
fi

if [ -z "$iso" ]; then
    set +e
    timeout 2 "$qemu" -machine q35 -nodefaults -device qemu-xhci,id=xhci \
        -device usb-kbd,bus=xhci.0 -vga std -display none -serial none -monitor none -S \
        >/dev/null 2>&1
    result=$?
    set -e
    if [ "$result" -ne 124 ]; then
        printf '%s\n' 'QEMU USB keyboard/VGA topology failed' >&2
        exit 1
    fi
    printf '%s\n' 'QEMU USB keyboard/VGA topology smoke passed; set ASTIA_USB_VGA_ISO for boot smoke'
    exit 0
fi

if [ ! -f "$iso" ]; then
    printf '%s\n' "USB VGA smoke ISO not found: $iso" >&2
    exit 1
fi

socket=$(mktemp -u /tmp/astia-usb-qmp.XXXXXX)
cleanup() { rm -f "$socket"; }
trap cleanup EXIT

set +e
timeout 12 "$qemu" -machine q35 -m "${ASTIA_VGA_MEMORY:-256M}" -cdrom "$iso" \
    -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -vga std -display none \
    -serial none -monitor none -qmp "unix:$socket,server=on,wait=off" >/dev/null 2>&1 &
pid=$!
set -e

python3 - "$socket" <<'PY'
import json
import socket
import sys
import time

path = sys.argv[1]
deadline = time.time() + 5
while time.time() < deadline:
    try:
        qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        qmp.connect(path)
        break
    except OSError:
        time.sleep(0.05)
else:
    raise SystemExit("QMP did not become available")

qmp.recv(4096)
qmp.sendall((json.dumps({"execute": "qmp_capabilities"}) + "\r\n").encode())
qmp.recv(4096)
for key in ("a", "b", "c", "ret"):
    qmp.sendall((json.dumps({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": key}]}}) + "\r\n").encode())
    qmp.recv(4096)
qmp.close()
PY

set +e
wait "$pid"
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    exit "$status"
fi
printf '%s\n' 'QEMU USB keyboard/VGA boot smoke passed; key events were delivered through QMP'
