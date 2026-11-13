#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BASE_INITRD ZSH_BINARY OUTPUT_INITRD" >&2
    exit 2
fi

base=$1
zsh_binary=$2
output=$3
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cpio=${ASTIA_CPIO:-cpio}
stage=$(mktemp -d /tmp/astia-zsh-initrd.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM

command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}
mkdir -p "$(dirname -- "$output")"
(cd "$stage" && "$cpio" -id --quiet --no-absolute-filenames) <"$base"
mkdir -p "$stage/bin"
cp "$zsh_binary" "$stage/bin/zsh"
ASTIA_CPIO="$cpio" sh "$root/kernel/tests/make_cpio_archive.sh" \
    "$stage" "$output" .
