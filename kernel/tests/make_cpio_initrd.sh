#!/bin/sh
# Build a tiny uncompressed newc or crc archive for the host initrd test.
set -e

format=$1
out=$2
cpio=${ASTIA_CPIO:-cpio}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

mkdir -p "$dir/sub"
printf 'alpha\n' > "$dir/alpha.txt"
printf '\336\255\276' > "$dir/beta.bin"
printf 'gamma\n' > "$dir/sub/gamma.txt"

(
    cd "$dir"
    printf '%s\n' alpha.txt beta.bin sub sub/gamma.txt |
        "$cpio" -o --format="$format" --quiet > "$out"
)
