#!/bin/sh
set -eu

out=${1:?image path required}
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

mkfs.ext4 -q -F -b 1024 -I 128 \
    -O '^has_journal,^64bit,^metadata_csum,^flex_bg,^dir_index,^extra_isize' \
    -d "$root" "$out" 16M
