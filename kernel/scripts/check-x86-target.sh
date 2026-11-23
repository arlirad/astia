#!/usr/bin/env bash
set -Eeuo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ASTIA_X86_KERNEL_BUILD:-${root}/build/x86-regression}"
cmake_bin="${CMAKE:-cmake}"

command -v "$cmake_bin" >/dev/null 2>&1 || {
    printf 'x86 kernel regression: error: cmake is not available\n' >&2
    exit 1
}

"$cmake_bin" -S "$root" -B "$build" -G Ninja \
    -DARCH=x86_64 \
    -DPLATFORM=virt \
    -DBOOT=virt \
    -DASTIA_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Debug
"$cmake_bin" --build "$build" --target astiakrn --parallel

printf '%s\n' 'x86 kernel target regression: PASS'
