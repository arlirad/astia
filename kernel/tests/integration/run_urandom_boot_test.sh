#!/bin/sh
# Boot the CSPRNG test image twice and require the /dev/urandom prefix to
# differ.  A deterministic or unseeded pool would replay the same bytes on
# the same QEMU command line.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/qemu_timeout.sh"

if [ "$#" -lt 3 ]; then
    echo "usage: $0 QEMU KERNEL INITRD" >&2
    exit 2
fi

qemu=$1
kernel=$2
initrd=$3
qemu_timeout_seconds=$(boot_timeout_seconds)

temporary=${TMPDIR:-/tmp}
first_log=$temporary/astia_urandom_first_$$.log
second_log=$temporary/astia_urandom_second_$$.log
trap 'rm -f "$first_log" "$second_log"' EXIT INT TERM

boot() {
    log=$1

    set +e
    timeout "$qemu_timeout_seconds" "$qemu" -kernel "$kernel" -initrd "$initrd" \
        -serial "file:$log" \
        -display none \
        -monitor none \
        -device isa-debug-exit \
        -no-reboot
    status=$?
    set -e

    if [ "$status" -ne 1 ]; then
        cat "$log"
        echo "urandom boot test: qemu status $status" >&2
        exit 1
    fi

    if ! grep -q "astia: random ok" "$log"; then
        cat "$log"
        echo "urandom boot test: CSPRNG checks did not pass" >&2
        exit 1
    fi

    value=$(sed -n 's/.*astia: urandom \([0-9a-f][0-9a-f]*\).*/\1/p' "$log" | tail -1)

    if [ -z "$value" ]; then
        cat "$log"
        echo "urandom boot test: missing urandom marker" >&2
        exit 1
    fi

    printf '%s\n' "$value"
}

first=$(boot "$first_log")
second=$(boot "$second_log")

if [ "$first" = "$second" ]; then
    echo "urandom boot test: identical /dev/urandom prefix across boots: $first" >&2
    exit 1
fi

echo "urandom fresh boot: PASS ($first != $second)"
