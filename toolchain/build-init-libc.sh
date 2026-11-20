#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${ASTIA_CROSS_BUILD:-/tmp/astia-clang-init-libc}
canonical_path() {
    case "$1" in
        /*) path=$1 ;;
        *) path=$PWD/$1 ;;
    esac
    if [ -d "$path" ]; then
        CDPATH= cd -- "$path" && pwd
        return
    else
        mkdir -p "$(dirname -- "$path")"
    fi
    path_dir=$(CDPATH= cd -- "$(dirname -- "$path")" && pwd)
    printf '%s/%s\n' "$path_dir" "$(basename -- "$path")"
}
mkdir -p "$build"
build=$(canonical_path "$build")
if [ "${ASTIA_SYSROOT+x}" = x ]; then
    sysroot=$ASTIA_SYSROOT
    external_sysroot=1
else
    sysroot=$build/sysroot
    external_sysroot=0
fi
mkdir -p "$sysroot"
sysroot=$(canonical_path "$sysroot")
target=${ASTIA_TARGET:-x86_64-unknown-elf}
clang=${CLANG:-clang}
clang_path=$(command -v "$clang")

libc_build=$build/libc
cache=$libc_build/CMakeCache.txt
if [ -f "$cache" ]; then
    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" | head -n 1)
    if [ -n "$cached_source" ] && [ "$cached_source" != "$root/libc" ]; then
        echo "stale libc CMake cache: $cached_source; removing $libc_build" >&2
        rm -rf -- "$libc_build"
        if [ "$external_sysroot" -eq 0 ] && [ "$sysroot" = "$build/sysroot" ]; then
            echo "removing generated default sysroot: $sysroot" >&2
            rm -rf -- "$sysroot"
        fi
    fi
fi

resource_dir=$($clang --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "clang compiler builtins not found below $resource_dir" >&2
    exit 1
fi

libc_revision=$(
    {
        find "$root/libc" -type f -exec sha256sum {} +
        sha256sum "$root/toolchain/build-init-libc.sh" \
            "$root/toolchain/astia-x86_64-clang.cmake" "$clang_path"
        "$clang" --version
        printf '%s\n' "$sysroot" "$target" "$external_sysroot"
    } | sort | sha256sum | awk '{print $1}'
)
if [ ! -f "$cache" ] || [ ! -f "$libc_build/.astia-config-key" ] ||
    [ "$(cat "$libc_build/.astia-config-key")" != "$libc_revision" ]; then
    cmake -S "$root/libc" -B "$libc_build" \
        -DCMAKE_TOOLCHAIN_FILE="$root/toolchain/astia-x86_64-clang.cmake" \
        -DASTIA_BUILD_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$sysroot"
    printf '%s\n' "$libc_revision" >"$libc_build/.astia-config-key"
fi
cmake --build "$libc_build" -j "${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
cmake --install "$libc_build"

mkdir -p "$build/image"
common_flags="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
compile_flags() {
    "$clang" --target="$target" -ffreestanding -fno-pic -fno-pie \
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone "$@"
}

image_revision=$(
    {
        sha256sum "$root/toolchain/build-init-libc.sh" \
            "$root/kernel/tests/crt0.S" "$root/kernel/tests/init_libc.c" \
            "$root/toolchain/astia-x86_64-clang.cmake" "$clang_path"
        "$clang" --version
        printf '%s\n' "$common_flags" "$resource_dir/include" \
            "$sysroot/include" "$target" "$sysroot"
    } | sha256sum | awk '{print $1}'
)
crt0_key=$(printf '%s\n' "$image_revision" | sha256sum | awk '{print $1}')
if [ ! -f "$build/image/crt0.o" ] ||
    [ ! -f "$build/image/crt0.key" ] ||
    [ "$(cat "$build/image/crt0.key")" != "$crt0_key" ]; then
    compile_flags -c -o "$build/image/crt0.o" "$root/kernel/tests/crt0.S"
    printf '%s\n' "$crt0_key" >"$build/image/crt0.key"
fi
init_key=$(
    {
        sha256sum "$root/kernel/tests/init_libc.c" \
            "$sysroot/lib/libastia_libc.a"
        printf '%s\n' "$image_revision"
    } | sha256sum | awk '{print $1}'
)
if [ ! -f "$build/image/init_libc.o" ] ||
    [ ! -f "$build/image/init_libc.key" ] ||
    [ "$(cat "$build/image/init_libc.key")" != "$init_key" ]; then
    compile_flags -fno-builtin -nostdinc \
        -isystem "$resource_dir/include" -isystem "$sysroot/include" -c \
        -o "$build/image/init_libc.o" "$root/kernel/tests/init_libc.c"
    printf '%s\n' "$init_key" >"$build/image/init_libc.key"
fi
link_key=$(
    {
        sha256sum "$build/image/crt0.o" "$build/image/init_libc.o" \
            "$sysroot/lib/libastia_libc.a" "$builtins"
        printf '%s\n' "$image_revision"
    } | sha256sum | awk '{print $1}'
)
if [ ! -f "$build/image/init_libc" ] ||
    [ ! -f "$build/image/init_libc.link-key" ] ||
    [ "$(cat "$build/image/init_libc.link-key")" != "$link_key" ]; then
    compile_flags -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
        -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
        -o "$build/image/init_libc" "$build/image/crt0.o" "$build/image/init_libc.o" \
        "$sysroot/lib/libastia_libc.a" "$builtins"
    printf '%s\n' "$link_key" >"$build/image/init_libc.link-key"
fi

file "$build/image/init_libc"
