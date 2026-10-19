#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 KERNEL_ELF TRACE_LOG" >&2
    exit 2
fi

kernel=$1
trace=$2

if command -v llvm-addr2line >/dev/null 2>&1; then
    addr2line=llvm-addr2line
else
    addr2line=addr2line
fi

grep -Eo 'rip=[0-9a-fA-F]+' "$trace" | sed 's/^rip=//' | while read -r address; do
    printf '0x%s -> ' "$address"
    "$addr2line" -f -C -e "$kernel" "0x$address"
done
