#!/bin/sh
set -eu

tool=${1:?tool path required}
work=${2:-${EXT4_STOCK_DIR:-${TMPDIR:-/tmp}/astia-ext4-stock}}

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

launcher=${EXT4_STOCK_VFS_LAUNCHER:-}
if [ -z "$launcher" ] && command -v setarch >/dev/null 2>&1; then
    launcher='setarch -R'
fi

run_case() {
    label=$1
    size=$2
    block_size=$3

    fixture=${work}/stock-${label}.img
    output=${work}/stock-${label}-out.img
    log=${work}/stock-${label}.log

    E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b "${block_size}" \
        -d "$root" "$fixture" "$size"
    e2fsck -fn "$fixture"

    if [ -n "$launcher" ]; then
        $launcher "$tool" "$fixture" "$output" normal >"$log" 2>&1
    else
        "$tool" "$fixture" "$output" normal >"$log" 2>&1
    fi

    cat "$log"

    if ! grep -qx 'OK' "$log"; then
        echo "ext4 stock VFS tool did not report OK for ${label}" >&2
        exit 1
    fi

    e2fsck -fn "$output"

    if dumpe2fs -h "$output" 2>/dev/null | grep -q 'needs_recovery'; then
        echo "stock ${label} output still needs recovery" >&2
        exit 1
    fi

    content=$(debugfs -R 'cat /result/persisted.txt' "$output" 2>/dev/null || true)
    printf '%s\n' "$content"

    if ! printf '%s\n' "$content" | grep -q 'persisted through ext4 module'; then
        echo "persisted.txt content missing from stock ${label} image" >&2
        exit 1
    fi
}

run_case 1k 16M 1024
run_case 4k 128M 4096

printf 'ext4 stock image test passed\n'
