#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${ASTIA_CROSS_BUILD:-/tmp/astia-clang-init-libc}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
clang=${CLANG:-clang}

resource_dir=$($clang --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "clang compiler builtins not found below $resource_dir" >&2
    exit 1
fi

cmake -S "$root/libc" -B "$build/libc" \
    -DCMAKE_TOOLCHAIN_FILE="$root/toolchain/astia-x86_64-clang.cmake" \
    -DASTIA_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$sysroot"
cmake --build "$build/libc" --clean-first -j "${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
cmake --install "$build/libc"

mkdir -p "$build/image"
common_flags="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
include_flags="-nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

# shellcheck disable=SC2086
$clang $common_flags -c -o "$build/image/crt0.o" "$root/kernel/tests/crt0.S"
# shellcheck disable=SC2086
$clang $common_flags -fno-builtin $include_flags -c \
    -o "$build/image/init_libc.o" "$root/kernel/tests/init_libc.c"
# shellcheck disable=SC2086
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$build/image/init_libc" "$build/image/crt0.o" "$build/image/init_libc.o" \
    "$sysroot/lib/libastia_libc.a" "$builtins"

file "$build/image/init_libc"
