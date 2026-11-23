#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
package=${NCURSES_PACKAGE:-$root/build/user/ncurses/package}
sysroot=${ASTIA_SYSROOT:-$root/build/user/ncurses/cross/sysroot}
crt0=${ASTIA_CRT0:-$root/build/user/ncurses/cross/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.cpio}
build=${NCURSES_SMOKE_BUILD:-$root/build/user/ncurses/smoke}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
probe_source=${NCURSES_SMOKE_SOURCE:-$root/toolchain/tests/ncurses_abi_probe.c}

for input in "$package/lib/libncurses.a" "$sysroot/lib/libastia_libc.a" \
    "$crt0" "$kernel" "$base" "$probe_source"; do
    if [ ! -f "$input" ]; then
        echo "ncurses QEMU smoke: missing input: $input" >&2
        exit 1
    fi
done

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtin_lib=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtin_lib" ]; then
    echo "ncurses QEMU smoke: clang compiler builtins not found" >&2
    exit 1
fi

common_flags="--target=$target -U__linux__ -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

rm -rf "$build"
mkdir -p "$build/root"
(cd "$build/root" && cpio -idmu < "$base" >/dev/null)
"$clang" $common_flags -I"$package/include" -c "$probe_source" \
    -o "$build/ncurses_smoke_probe.o"
"$clang" $common_flags -nostdlib -nostartfiles -nodefaultlibs -static \
    -Wl,-no-pie -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$build/root/sbin/init" "$crt0" "$build/ncurses_smoke_probe.o" \
    "$package/lib/libncurses.a" "$sysroot/lib/libastia_libc.a" "$builtin_lib"
(cd "$build/root" && find . -print | cpio -o --format=newc --quiet) >"$build/initrd.cpio"

python3 - "$qemu" "$kernel" "$build/initrd.cpio" <<'PY'
import os
import re
import select
import subprocess
import sys
import termios
import time

qemu, kernel, initrd = sys.argv[1:]
process = subprocess.Popen(
    [qemu, "-m", "128M", "-kernel", kernel, "-initrd", initrd,
     "-serial", "pty", "-display", "none", "-monitor", "none",
     "-no-reboot", "-no-shutdown"],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, close_fds=True)
serial_name = b""
while b"/dev/pts/" not in serial_name:
    ready, _, _ = select.select([process.stdout], [], [], 5)
    if not ready:
        raise SystemExit("ncurses QEMU smoke: qemu did not create a serial PTY")
    chunk = os.read(process.stdout.fileno(), 4096)
    if not chunk:
        raise SystemExit("ncurses QEMU smoke: qemu did not create a serial PTY")
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
key_time = None
saw_marker = False
while time.time() < deadline:
    ready, _, _ = select.select([master], [], [], 0.2)
    if ready:
        try:
            buffer.extend(os.read(master, 4096))
        except OSError:
            break
    if not saw_marker and b"NCURSES_ABI_OK" in buffer:
        saw_marker = True
        os.write(master, b"x")
        key_time = time.time()
    if saw_marker and time.time() - key_time > 2:
        break
if process.poll() is None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
os.close(master)
if not saw_marker:
    sys.stderr.write(bytes(buffer).decode("utf-8", "replace")[-4000:])
    raise SystemExit("ncurses QEMU smoke: missing NCURSES_ABI_OK")
print("ncurses QEMU smoke: PASS (fallback vt220, exact NCURSES_ABI_OK output)")
PY
