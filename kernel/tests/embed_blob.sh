#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 INPUT SYMBOL OUTPUT" >&2
    exit 2
fi

input=$1
symbol=$2
output=$3
xxd=${XXD:-xxd}
temporary=$output.tmp
trap 'rm -f "$temporary"' EXIT INT TERM
"$xxd" -i -n "$symbol" "$input" > "$temporary"
mv "$temporary" "$output"
