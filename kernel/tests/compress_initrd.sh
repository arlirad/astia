#!/bin/sh
set -e

kind=$1
compressor=$2
input=$3
output=$4

case "$kind" in
    gzip)
        "$compressor" -c "$input" > "$output"
        ;;
    zstd)
        "$compressor" -q --check -c "$input" > "$output"
        ;;
    zstd-nosize)
        "$compressor" -q --check --no-content-size -c "$input" > "$output"
        ;;
    *)
        echo "compress_initrd: unknown kind: $kind" >&2
        exit 2
        ;;
esac
