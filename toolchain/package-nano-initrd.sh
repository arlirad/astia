#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BASE_INITRD NANO_BINARY OUTPUT_INITRD" >&2
    exit 2
fi
base=$1
nano=$2
output=$3
stage=$(mktemp -d /tmp/astia-nano-initrd.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM
tar xf "$base" -C "$stage"
mkdir -p "$stage/bin"
cp "$nano" "$stage/bin/nano"
mkdir -p "$(dirname -- "$output")"
tar cf "$output" -C "$stage" .
