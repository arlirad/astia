#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${NANO_SMOKE_BUILD:-/tmp/astia-nano-smoke}
sysroot=${ASTIA_SYSROOT:-/tmp/astia-nano-cross2/sysroot}
crt0=${ASTIA_CRT0:-/tmp/astia-nano-cross2/image/crt0.o}
nano=${NANO_BINARY:-/tmp/astia-nano-real19/output/nano}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.cpio}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=x86_64-unknown-elf
resource=$($clang --target=$target -print-resource-dir)

if [ ! -f "$nano" ] || [ ! -f "$kernel" ] || [ ! -f "$base" ]; then
    echo "nano smoke inputs missing; build nano and kernel first" >&2
    exit 1
fi
mkdir -p "$build/root/bin" "$build/root/sbin" "$build/root/etc"
tarball=$build/root.tar
rm -f "$tarball"
tar cf "$tarball" -C "$build/root" .
rm -rf "$build/root"
mkdir -p "$build/root/bin" "$build/root/sbin" "$build/root/etc"
(cd "$build/root" && cpio -idmu < "$base" >/dev/null)
mkdir -p "$build/root/tmp"
cp "$nano" "$build/root/bin/nano"
common="--target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource/include -isystem $sysroot/include"
init_cppflags=
if [ "${NANO_DIRECT_EXEC:-0}" = 1 ]; then
    init_cppflags=-DNANO_DIRECT_EXEC
fi
# shellcheck disable=SC2086
$clang $common $init_cppflags -c "$root/toolchain/nano-pty-init.c" -o "$build/nano-init.o"
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$build/root/sbin/init" \
    "$crt0" "$build/nano-init.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
(cd "$build/root" && find . -print | cpio -o --format=newc --quiet) > "$build/initrd.cpio"

python3 - "$qemu" "$kernel" "$build/initrd.cpio" <<'PY'
import os
import select
import re
import subprocess
import sys
import termios
import time

qemu, kernel, initrd = sys.argv[1:]
direct = os.environ.get("NANO_DIRECT_EXEC") == "1"
command = [qemu, "-m", "128M", "-kernel", kernel, "-initrd", initrd,
           "-serial", "pty", "-display", "none", "-monitor", "none",
           "-no-reboot", "-no-shutdown"]
process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           close_fds=True)
serial_name = b""
while b"/dev/pts/" not in serial_name:
    ready, _, _ = select.select([process.stdout], [], [], 5)
    if not ready:
        raise SystemExit("qemu did not create a serial PTY")
    chunk = os.read(process.stdout.fileno(), 4096)
    if not chunk:
        raise SystemExit("qemu did not create a serial PTY")
    serial_name += chunk
serial_path = re.search(rb"/dev/pts/[0-9]+", serial_name).group(0).decode()
master = os.open(serial_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attributes = termios.tcgetattr(master)
attributes[0] = 0
attributes[1] = 0
attributes[2] &= ~(termios.CSIZE | termios.PARENB)
attributes[2] |= termios.CS8
attributes[3] = 0
attributes[4] = termios.B115200
attributes[5] = termios.B115200
termios.tcsetattr(master, termios.TCSANOW, attributes)
deadline = time.time() + 45
buffer = bytearray()
stage = 0
stage_deadline = time.time() + 45
while time.time() < deadline and process.poll() is None:
    ready, _, _ = select.select([master], [], [], 0.2)
    if ready:
        try:
            buffer.extend(os.read(master, 4096))
        except OSError:
            break
    if stage == 0 and b"GNU nano" in buffer:
        os.write(master, b"Astia nano smoke\n")
        stage = 1
        stage_deadline = time.time() + 5
    elif stage == 1 and time.time() > stage_deadline:
        os.write(master, b"\x18")
        stage = 2
        stage_deadline = time.time() + 5
    elif stage == 2 and time.time() > stage_deadline:
        os.write(master, b"y")
        stage = 3
        stage_deadline = time.time() + 2
    elif stage == 3 and time.time() > stage_deadline:
        os.write(master, b"\r")
        stage = 4
        stage_deadline = time.time() + 5
    elif stage == 4 and b"NANO_SMOKE_OK" in buffer:
        stage = 5
if process.poll() is None:
    process.terminate()
    process.wait(timeout=5)
os.close(master)
output = bytes(buffer)
if direct:
    print("nano direct-exec smoke: process exited")
    raise SystemExit(0)
if b"NANO_SMOKE_OK" not in output:
    sys.stderr.write(output.decode("utf-8", "replace")[-4000:])
    raise SystemExit("nano QEMU smoke failed")
print("nano QEMU smoke: PASS")
PY
