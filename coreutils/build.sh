#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${1:-${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}}
clang=${CLANG:-clang}
target=x86_64-unknown-elf
resource_dir=$($clang --target=$target -print-resource-dir)
builtins=$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a
out=${COREUTILS_BUILD:-$root/coreutils/build}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

if [ ! -f "$sysroot/lib/libastia_libc.a" ]; then
    echo "Astia sysroot missing: $sysroot/lib/libastia_libc.a" >&2
    exit 1
fi
if [ ! -f "$crt0" ]; then
    echo "Astia crt0 missing: $crt0" >&2
    exit 1
fi

mkdir -p "$out/obj" "$out/bin"
common="--target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

for source in "$root"/coreutils/src/*.c; do
    name=$(basename "$source" .c)
    # shellcheck disable=SC2086
    if [ ! -f "$out/obj/$name.o" ] || [ "$source" -nt "$out/obj/$name.o" ] ||
        [ "$sysroot/lib/libastia_libc.a" -nt "$out/obj/$name.o" ]; then
        $clang $common -c "$source" -o "$out/obj/$name.o"
    fi
    # shellcheck disable=SC2086
    if [ ! -f "$out/bin/$name" ] || [ "$out/obj/$name.o" -nt "$out/bin/$name" ] ||
        [ "$crt0" -nt "$out/bin/$name" ] ||
        [ "$sysroot/lib/libastia_libc.a" -nt "$out/bin/$name" ]; then
        $clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
            -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
            -o "$out/bin/$name" "$crt0" "$out/obj/$name.o" \
            "$sysroot/lib/libastia_libc.a" "$builtins"
    fi
done

printf 'built Astia coreutils:'
for binary in "$out"/bin/*; do printf ' %s' "$(basename "$binary")"; done
printf '\n'
