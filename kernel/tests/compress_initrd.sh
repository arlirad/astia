#!/bin/sh
set -e

kind=$1
compressor=$2
input=$3
output=$4

if [ "$kind" = gzip ]; then
    "$compressor" -c "$input" > "$output"
else
    "$compressor" -q -c "$input" > "$output"
fi
