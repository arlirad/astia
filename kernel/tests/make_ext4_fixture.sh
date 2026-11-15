#!/bin/sh
set -eu

out=${1:?image path required}
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
export E2FSPROGS_FAKE_TIME=1700000000
mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

mkfs.ext4 -q -F -b 1024 -I 128 \
    -U 00112233-4455-6677-8899-aabbccddeeff \
    -E hash_seed=01234567-89ab-cdef-0123-456789abcdef \
    -O '^has_journal,^64bit,^flex_bg' \
    -d "$root" "$out" 16M
e2fsck -fn "$out" >/dev/null
