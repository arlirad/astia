#!/bin/sh
set -eu

launcher=${1:-}
tool=${2:?tool path required}

work=${EXT4_RW_DIR:-${TMPDIR:-/tmp}/astia-ext4-rw}
fixture=${work}/fixture.img
out=${work}/result.img

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -I 256 \
    -O '^has_journal,^64bit,^flex_bg,^orphan_file,^metadata_csum_seed' \
    -d "$root" "$fixture" 16M
e2fsck -fn "$fixture"

if [ -n "$launcher" ]; then
    "$launcher" -R "$tool" "$fixture" "$out" >"${work}/tool.log" 2>&1
else
    "$tool" "$fixture" "$out" >"${work}/tool.log" 2>&1
fi

cat "${work}/tool.log"

if ! grep -qx 'OK' "${work}/tool.log"; then
    echo "ext4 read-write VFS tool did not report OK" >&2
    exit 1
fi

e2fsck -fn "$out"

content=$(debugfs -R 'cat /result/persisted.txt' "$out" 2>/dev/null)
printf '%s\n' "$content"

if ! printf '%s\n' "$content" | grep -q 'persisted through ext4 module'; then
    echo "persisted.txt content missing" >&2
    exit 1
fi

listing=$(debugfs -R 'ls -l /' "$out" 2>/dev/null)
printf '%s\n' "$listing"

for name in etc dir result; do
    if ! printf '%s\n' "$listing" | grep -Eq "(^|[[:space:]])${name}$"; then
        echo "expected directory ${name} missing from /" >&2
        exit 1
    fi
done

for name in hello sub; do
    if printf '%s\n' "$listing" | grep -Eq "(^|[[:space:]])${name}$"; then
        echo "removed node ${name} survived the delete phase" >&2
        exit 1
    fi
done

before=$(debugfs -R 'stat /etc/known.txt' "$fixture" 2>/dev/null)
after=$(debugfs -R 'stat /etc/known.txt' "$out" 2>/dev/null)

if [ "$before" != "$after" ]; then
    echo "/etc/known.txt changed" >&2
    printf '%s\n' "$before" >&2
    printf '%s\n' "$after" >&2
    exit 1
fi

printf 'ext4 read-write VFS test passed\n'
