#!/bin/sh
set -eu

image=${1:?image path required}
out=${2:?output directory required}
tool=${EXT4_DIR_TOOL:?EXT4_DIR_TOOL must name the ext4_dir_unit binary}

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

case "$result" in
*PHASE1*OK) ;;
*)
    echo "ext4_dir fixture tool did not report PHASE1 and OK" >&2
    exit 1
    ;;
esac

e2fsck -fn "$out/work.img"

listing=$(debugfs -R 'ls -l /' "$out/work.img" 2>/dev/null)
printf '%s\n' "$listing"

if printf '%s\n' "$listing" | grep -Eq '(^|[[:space:]])newdir$'; then
    echo "newdir survived the delete phase" >&2
    exit 1
fi

for name in etc dir; do
    if ! printf '%s\n' "$listing" | grep -Eq "(^|[[:space:]])${name}$"; then
        echo "fixture directory ${name} missing from /" >&2
        exit 1
    fi
done

size=$(debugfs -R 'stat /etc/known.txt' "$out/work.img" 2>/dev/null |
    sed -n 's/^.*Size: \([0-9][0-9]*\).*$/\1/p' | head -n 1)

if [ "$size" != "1500" ]; then
    echo "unexpected /etc/known.txt size: ${size:-missing}" >&2
    exit 1
fi

e2fsck -fn "$out/work.img"
