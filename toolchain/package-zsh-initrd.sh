#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BASE_INITRD ZSH_BINARY OUTPUT_INITRD" >&2
    exit 2
fi

base=$1
zsh_binary=$2
output=$3
stage=$(mktemp -d /tmp/astia-zsh-initrd.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM

mkdir -p "$(dirname -- "$output")"
tar xf "$base" -C "$stage"
mkdir -p "$stage/bin"
cp "$zsh_binary" "$stage/bin/zsh"
tar cf "$output" -C "$stage" .
