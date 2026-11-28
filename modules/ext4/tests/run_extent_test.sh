#!/bin/sh
set -eu

image=${1:?image path required}
out=${2:?output directory required}
tool=${EXT4_EXTENT_TOOL:?EXT4_EXTENT_TOOL must name the ext4_extent_unit binary}

mkdir -p "$(dirname "$image")" "$out"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -I 256 \
    -O '^has_journal,^64bit,^flex_bg,^orphan_file,^metadata_csum_seed' \
    -d "$root" "$image" 16M
e2fsck -fn "$image"

cp "$image" "$out/work.img"

result=$("$tool" "$out/work.img")
printf '%s\n' "$result"

if [ "$result" != "OK" ]; then
    echo "ext4_extent fixture tool did not report OK" >&2
    exit 1
fi

size=$(debugfs -R 'stat /etc/known.txt' "$out/work.img" 2>/dev/null |
    sed -n 's/^.*Size: \([0-9][0-9]*\).*$/\1/p' | head -n 1)

if [ "$size" != "5122048" ]; then
    echo "unexpected /etc/known.txt size: ${size:-missing}" >&2
    exit 1
fi

e2fsck -fn "$out/work.img"
