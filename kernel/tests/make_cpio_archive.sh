#!/bin/sh
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 ROOT OUTPUT PATH..." >&2
    exit 2
fi

root=$1
output=$2
shift 2
cpio=${ASTIA_CPIO:-cpio}
output_dir=$(CDPATH= cd -- "$(dirname -- "$output")" && pwd)
output=$output_dir/$(basename -- "$output")
temporary=$output.$$
trap 'rm -f "$temporary"' EXIT

command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}

(
    cd "$root"
    find "$@" -print | "$cpio" -o --format=newc --quiet >"$temporary"
)
mv -f "$temporary" "$output"
