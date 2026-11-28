#!/bin/sh
set -eu

tool=${1:?tool path required}
work=${2:-${EXT4_JOURNAL_VFS_DIR:-${TMPDIR:-/tmp}/astia-ext4-journal-vfs}}
fixture=${work}/fixture.img

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -I 256 \
    -O 'has_journal,^64bit,^flex_bg,^orphan_file,^metadata_csum_seed' \
    -J size=4 -d "$root" "$fixture" 16M
e2fsck -fn "$fixture"

LC_ALL=C awk 'BEGIN { for (i = 0; i < 8192; ++i) printf "%c", (i * 37 + 11) % 256 }' \
    >"${work}/expected.bin"

launcher=${EXT4_JOURNAL_VFS_LAUNCHER:-}
if [ -z "$launcher" ] && command -v setarch >/dev/null 2>&1; then
    launcher='setarch -R'
fi

run_tool() {
    mode=$1
    image=$2
    output=$3

    if [ -n "$launcher" ]; then
        $launcher "$tool" "$image" "$output" "$mode"
    else
        "$tool" "$image" "$output" "$mode"
    fi
}

expect_ok() {
    log=$1

    cat "$log"

    if ! grep -qx 'OK' "$log"; then
        echo "ext4 journal VFS tool did not report OK for $(basename "$log")" >&2
        exit 1
    fi
}

needs_recovery() {
    dumpe=$(dumpe2fs -h "$1" 2>/dev/null || true)
    printf '%s\n' "$dumpe" | grep -q 'needs_recovery'
}

check_recovered() {
    image=$1

    e2fsck -fn "$image"

    if needs_recovery "$image"; then
        echo "recovered image ${image} still needs recovery" >&2
        exit 1
    fi

    stat=$(debugfs -R 'stat /crash/keep.txt' "$image" 2>/dev/null || true)
    printf '%s\n' "$stat"

    size=$(printf '%s\n' "$stat" |
        sed -n 's/^.*Size: \([0-9][0-9]*\).*$/\1/p' | head -n 1)

    if [ "$size" != "8192" ]; then
        echo "unexpected recovered /crash/keep.txt size in ${image}: ${size:-missing}" >&2
        exit 1
    fi

    debugfs -R 'cat /crash/keep.txt' "$image" 2>/dev/null \
        >"${work}/recovered.bin" || true

    if ! cmp "${work}/expected.bin" "${work}/recovered.bin"; then
        echo "recovered /crash/keep.txt content does not match in ${image}" >&2
        exit 1
    fi
}

cp "$fixture" "${work}/normal.img"
run_tool normal "${work}/normal.img" "${work}/normal-out.img" \
    >"${work}/normal.log" 2>&1
expect_ok "${work}/normal.log"

e2fsck -fn "${work}/normal-out.img"

content=$(debugfs -R 'cat /result/persisted.txt' \
    "${work}/normal-out.img" 2>/dev/null || true)
printf '%s\n' "$content"

if ! printf '%s\n' "$content" | grep -q 'persisted through ext4 module'; then
    echo "persisted.txt content missing from journaled image" >&2
    exit 1
fi

if needs_recovery "${work}/normal-out.img"; then
    echo "mounted journal image still needs recovery" >&2
    exit 1
fi

cp "$fixture" "${work}/crash.img"
run_tool crash "${work}/crash.img" "${work}/crash.img" \
    >"${work}/crash.log" 2>&1
expect_ok "${work}/crash.log"

if ! needs_recovery "${work}/crash.img"; then
    echo "crash image is missing needs_recovery" >&2
    exit 1
fi

cp "${work}/crash.img" "${work}/recovered.img"
run_tool recover "${work}/recovered.img" "${work}/recovered.img" \
    >"${work}/recovered.log" 2>&1
expect_ok "${work}/recovered.log"

check_recovered "${work}/recovered.img"

cp "$fixture" "${work}/crash-checkpoint.img"
run_tool crash-checkpoint "${work}/crash-checkpoint.img" \
    "${work}/crash-checkpoint.img" >"${work}/crash-checkpoint.log" 2>&1
expect_ok "${work}/crash-checkpoint.log"

if ! needs_recovery "${work}/crash-checkpoint.img"; then
    echo "checkpoint crash image is missing needs_recovery" >&2
    exit 1
fi

cp "${work}/crash-checkpoint.img" "${work}/recovered-checkpoint.img"
run_tool recover "${work}/recovered-checkpoint.img" \
    "${work}/recovered-checkpoint.img" \
    >"${work}/recovered-checkpoint.log" 2>&1
expect_ok "${work}/recovered-checkpoint.log"

check_recovered "${work}/recovered-checkpoint.img"

printf 'ext4 journal VFS test passed\n'
