#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${1:-${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}}
clang=${CLANG:-clang}
target=x86_64-unknown-elf
clang_path=$(command -v "$clang")
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

mkdir -p "$out/obj" "$out/bin" "$out/.keys"
source_manifest=$out/.keys/sources
next_source_manifest=$source_manifest.$$
for source in "$root"/coreutils/src/*.c; do
    basename "$source" .c
done | sort -u >"$next_source_manifest"
if [ -f "$source_manifest" ]; then
    while IFS= read -r managed_name; do
        if ! grep -Fqx "$managed_name" "$next_source_manifest"; then
            rm -f "$out/bin/$managed_name" "$out/obj/$managed_name.o" \
                "$out/obj/$managed_name.o.key" "$out/.keys/$managed_name" \
                "$out/bin/$managed_name.key"
        fi
        rm -f "$out/bin/$managed_name.key"
    done <"$source_manifest"
fi
while IFS= read -r managed_name; do
    rm -f "$out/bin/$managed_name.key"
done <"$next_source_manifest"
mv "$next_source_manifest" "$source_manifest"
compile_flags() {
    "$clang" --target="$target" -ffreestanding -fno-builtin -fPIE \
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
        -nostdinc -isystem "$resource_dir/include" -isystem "$sysroot/include" "$@"
}

toolchain_revision=$(
    {
        sha256sum "$root/coreutils/build.sh" \
            "$root/toolchain/build-init-libc.sh" "$clang_path"
        "$clang" --version
        printf '%s\n' "$target" "$resource_dir" "$sysroot"
    } | sha256sum | awk '{print $1}'
)

for source in "$root"/coreutils/src/*.c; do
    name=$(basename "$source" .c)
    object_key=$(
        {
            sha256sum "$source" "$sysroot/lib/libastia_libc.a" "$sysroot/lib/libc.so"
            printf '%s\n' "$toolchain_revision"
        } | sha256sum | awk '{print $1}'
    )
    if [ ! -f "$out/obj/$name.o" ] ||
        [ ! -f "$out/obj/$name.o.key" ] ||
        [ "$(cat "$out/obj/$name.o.key")" != "$object_key" ]; then
        compile_flags -c "$source" -o "$out/obj/$name.o"
        printf '%s\n' "$object_key" >"$out/obj/$name.o.key"
    fi
    link_key=$(
        {
            sha256sum "$out/obj/$name.o" "$crt0" \
                "$sysroot/lib/libastia_libc.a" "$sysroot/lib/libc.so" "$builtins"
            printf '%s\n' "$toolchain_revision"
        } | sha256sum | awk '{print $1}'
    )
    if [ ! -f "$out/bin/$name" ] ||
        [ ! -f "$out/.keys/$name" ] ||
        [ "$(cat "$out/.keys/$name")" != "$link_key" ]; then
        compile_flags -nostdlib -Wl,-Bdynamic -Wl,-pie -Wl,-e,_start \
            -Wl,--dynamic-linker=/lib/ld.astia.so -Wl,-rpath-link,"$sysroot/lib" \
            -o "$out/bin/$name" "$crt0" "$out/obj/$name.o" \
            "$sysroot/lib/libc.so" "$builtins"
        printf '%s\n' "$link_key" >"$out/.keys/$name"
    fi
done

printf 'built Astia coreutils:'
for binary in "$out"/bin/*; do printf ' %s' "$(basename "$binary")"; done
printf '\n'
