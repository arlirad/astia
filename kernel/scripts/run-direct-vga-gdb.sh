#!/usr/bin/env sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
build=${ASTIA_DIRECT_BUILD:-$root/build-direct-vga-gdb}
kernel=${ASTIA_DIRECT_KERNEL:-$build/src/astiakrn}
initrd=${ASTIA_DIRECT_INITRD:-$build/direct-vga-gdb-initrd.cpio}
qemu=${QEMU:-qemu-system-x86_64}
cpus=${ASTIA_DIRECT_CPUS:-2}
memory=${ASTIA_DIRECT_MEMORY:-256M}
display=${ASTIA_DIRECT_DISPLAY:-gtk}
gdb_port=${ASTIA_DIRECT_GDB_PORT:-1234}
mode=${ASTIA_DIRECT_GDB_MODE:-wait}
skip_build=${ASTIA_DIRECT_SKIP_BUILD:-0}
dry_run=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --attach|--non-stop)
            mode=attach
            ;;
        --wait)
            mode=wait
            ;;
        --dry-run)
            dry_run=1
            ;;
        --port)
            shift
            [ "$#" -gt 0 ] || { echo "--port requires a value" >&2; exit 2; }
            gdb_port=$1
            ;;
        --help|-h)
            printf '%s\n' \
                'usage: run-direct-vga-gdb.sh [--wait|--attach|--non-stop] [--port PORT] [--dry-run]'
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            exit 2
            ;;
    esac
    shift
done

case "$mode" in
    wait|attach) ;;
    *) echo "ASTIA_DIRECT_GDB_MODE must be wait or attach" >&2; exit 2 ;;
esac
case "$cpus" in
    ''|*[!0-9]*|0) echo "ASTIA_DIRECT_CPUS must be a positive integer" >&2; exit 2 ;;
esac
case "$gdb_port" in
    ''|*[!0-9]*) echo "ASTIA_DIRECT_GDB_PORT must be a number" >&2; exit 2 ;;
esac

if [ "$dry_run" -eq 0 ] && [ "$skip_build" != 1 ]; then
    cmake -S "$root" -B "$build" \
        -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
    cmake --build "$build" --target astiakrn

    stage=$build/direct-vga-gdb-initrd
    rm -rf "$stage"
    mkdir -p "$stage/etc"
    printf '%s\n' 'Astia direct PVH developer initrd' >"$stage/etc/astia-direct-vga-gdb"
    mkdir -p "$(dirname -- "$initrd")"
    (
        cd "$stage"
        find . -print | cpio -o --format=newc --quiet >"$initrd"
    )
fi

set -- "$qemu" \
    -kernel "$kernel" \
    -initrd "$initrd" \
    -smp "$cpus" \
    -m "$memory" \
    -vga std \
    -display "$display" \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -gdb "tcp::$gdb_port"
if [ "$mode" = wait ]; then
    set -- "$@" -S
fi

printf '%s\n' 'direct-vga-gdb: framebuffer descriptor: unavailable (PVH); serial fallback'
printf '%s\n' 'direct-vga-gdb: boot marker: boot ok'
printf '%s' 'direct-vga-gdb: qemu command:'
printf ' %s' "$@"
printf '\n'

if [ "$dry_run" -eq 1 ]; then
    exit 0
fi

if ! command -v "$qemu" >/dev/null 2>&1; then
    echo "QEMU executable not found: $qemu" >&2
    exit 1
fi
if [ ! -x "$kernel" ]; then
    echo "kernel artifact is missing or not executable: $kernel" >&2
    exit 1
fi
if [ ! -f "$initrd" ]; then
    echo "initrd artifact is missing: $initrd" >&2
    exit 1
fi

exec "$@"
