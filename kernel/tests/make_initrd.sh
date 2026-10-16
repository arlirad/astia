#!/bin/sh
# Build a tiny initrd tarball for the host initrd test.
#
#   usage: make_initrd.sh <output.tar>
#
# Produces an archive with three regular files (one of them binary, to prove
# bytes round-trip) plus a subdirectory entry, so the reader's directory
# skipping is exercised too.
set -e
out=$1
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

mkdir -p "$dir/sub"
printf 'alpha\n' > "$dir/alpha.txt"
printf '\336\255\276' > "$dir/beta.bin" # 0xde 0xad 0xbe
printf 'gamma\n' > "$dir/sub/gamma.txt"

# `sub` recurses in sub/gamma.txt and records the `sub/` directory entry.
tar cf "$out" -C "$dir" alpha.txt beta.bin sub
