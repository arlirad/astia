#!/usr/bin/env sh
set -eu

qemu=${1:?usage: run-qemu-load.sh <qemu> <iso> <serial-log>}
iso=${2:?}
log=${3:?}
python=${PYTHON:-python3}

"$python" - "$qemu" "$iso" "$log" <<'PY'
import json
import os
import socket
import subprocess
import sys
import time

qemu, iso, log = sys.argv[1:4]
qmp_path = log + ".qmp"
timeout = float(os.environ.get("ASTIA_PS2_TIMEOUT", "60"))

for path in (log, qmp_path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass

command = [
    qemu,
    "-machine", "q35",
    "-m", "256M",
    "-cdrom", iso,
    "-vga", "std",
    "-display", "none",
    "-serial", "file:" + log,
    "-qmp", "unix:%s,server=on,wait=off" % qmp_path,
    "-no-reboot",
    "-no-shutdown",
]
process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                           stderr=subprocess.STDOUT)
deadline = time.monotonic() + timeout


def serial_data():
    try:
        with open(log, "rb") as stream:
            return stream.read()
    except FileNotFoundError:
        return b""


def wait_serial(needle, description):
    while time.monotonic() < deadline:
        data = serial_data()
        if needle in data:
            return data
        if process.poll() is not None:
            raise RuntimeError("QEMU exited while waiting for %s" % description)
        time.sleep(0.05)
    raise RuntimeError("timed out waiting for %s" % description)


def connect_qmp():
    while time.monotonic() < deadline:
        try:
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.connect(qmp_path)
            connection.settimeout(5)
            connection.recv(4096)
            connection.sendall((json.dumps({"execute": "qmp_capabilities"}) + "\r\n").encode())
            connection.recv(4096)
            return connection
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("QMP did not become available")


def qmp(connection, payload):
    connection.sendall((json.dumps(payload) + "\r\n").encode())
    response = b""
    while b"\r\n" not in response:
        part = connection.recv(4096)
        if not part:
            break
        response += part
    return response


try:
    wait_serial(b"ps2-keyboard: resident module loaded", "the module load marker")
    wait_serial(b"ps2-keyboard: ready irq=1", "the PS/2 ready/IRQ marker")
    wait_serial(b"PS2_READY", "the ttv0 probe readiness marker")
    wait_serial(b"framebuffer: ready", "the GRUB framebuffer marker")

    qmp_connection = connect_qmp()
    for key in ("a", "b", "c", "ret"):
        response = qmp(qmp_connection, {
            "execute": "send-key",
            "arguments": {"keys": [{"type": "qcode", "data": key}]},
        })
        if b'"error"' in response:
            raise RuntimeError("QMP send-key failed: %r" % response)

    wait_serial(b"PS2_KEYS:abc", "the QMP-injected ttv0 marker")
    try:
        qmp(qmp_connection, {"execute": "quit"})
    except OSError:
        pass
    qmp_connection.close()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
    sys.stdout.buffer.write(serial_data())
    raise SystemExit(0)
except Exception as error:
    sys.stdout.buffer.write(serial_data())
    print("run-qemu-load: %s" % error, file=sys.stderr)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
    raise SystemExit(1)
PY
