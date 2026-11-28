#!/bin/sh
set -eu

base=${EXT4_JOURNAL_DIR:-${TMPDIR:-/tmp}/astia-ext4-journal}
image=${1:-$base/fixture.img}
out=${2:-$base}
tool=${EXT4_JOURNAL_TOOL:?EXT4_JOURNAL_TOOL must name the ext4_journal_unit binary}

mkdir -p "$(dirname "$image")" "$out" "$base"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -I 256 \
    -O 'has_journal,^64bit,^flex_bg,^orphan_file,^metadata_csum_seed' \
    -J size=4 -d "$root" "$image" 16M
e2fsck -fn "$image"

printf 'journal replay payload\n' >"$out/expected.bin"
dd if=/dev/zero bs=1 count=$((128 - $(wc -c <"$out/expected.bin"))) \
    >>"$out/expected.bin" 2>/dev/null

check_content() {
    img=$1

    debugfs -R 'cat /etc/known.txt' "$img" 2>/dev/null >"$out/content.bin"
    cmp "$out/content.bin" "$out/expected.bin"

    size=$(debugfs -R 'stat /etc/known.txt' "$img" 2>/dev/null |
        sed -n 's/^.*Size: \([0-9][0-9]*\).*$/\1/p' | head -n 1)

    if [ "$size" != "128" ]; then
        echo "unexpected /etc/known.txt size: ${size:-missing}" >&2
        exit 1
    fi
}

cp "$image" "$out/commit.img"
"$tool" --commit "$out/commit.img" "$out/commit.img"
e2fsck -fn "$out/commit.img"
check_content "$out/commit.img"

cp "$image" "$out/crash.img"
"$tool" --crash "$out/crash.img" "$out/crash.img"

dumpe2fs -h "$out/crash.img" >"$out/crash.dumpe2fs" 2>/dev/null || true

if ! grep -q 'needs_recovery' "$out/crash.dumpe2fs"; then
    echo "crash image is missing needs_recovery" >&2
    exit 1
fi

"$tool" --recover "$out/crash.img" "$out/crash.img"
e2fsck -fn "$out/crash.img"

dumpe2fs -h "$out/crash.img" >"$out/recovered.dumpe2fs" 2>/dev/null || true

if grep -q 'needs_recovery' "$out/recovered.dumpe2fs"; then
    echo "recovered image still needs recovery" >&2
    exit 1
fi

check_content "$out/crash.img"

cp "$image" "$out/revoke.img"
"$tool" --revoke-reset "$out/revoke.img" "$out/revoke.img"
