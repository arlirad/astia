#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_source=${ZSH_SOURCE:-${1:-}}
if [ -z "$zsh_source" ]; then
    echo "usage: ZSH_SOURCE=/path/to/zsh $0" >&2
    exit 2
fi

zsh_source=$(CDPATH= cd -- "$zsh_source" && pwd)
build=${ZSH_BUILD:-/tmp/astia-zsh-build}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
clang=${CLANG:-clang}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
resource_dir=$($clang --target=x86_64-unknown-elf -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)

if [ ! -x "$zsh_source/configure" ]; then
    (cd "$zsh_source" && ./Util/preconfig)
fi

# Kept as a source-tree validation hook; the hook is intentionally a no-op.
"$root/toolchain/patch-zsh-astia.sh" "$zsh_source"

ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
    "$root/toolchain/build-init-libc.sh"

mkdir -p "$build"
cat > "$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=x86_64-unknown-elf -ffreestanding -fno-builtin \
-fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
-mno-red-zone -nostdinc -isystem "$resource_dir/include" \
-isystem "$sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"

cd "$build"
"$zsh_source/configure" \
    --host=x86_64-unknown-elf \
    --build=x86_64-pc-linux-gnu \
    CC="$build/astia-cc" \
    CFLAGS='-O0 -DASTIA' \
    LDFLAGS='-nostdlib -static -no-pie -Wl,-Ttext-segment=0x40000000' \
    LIBS="$build/astia/image/crt0.o $sysroot/lib/libastia_libc.a $builtins"

make -C "$build" -j "$jobs"
mkdir -p "$build/output"
cp "$build/Src/zsh" "$build/output/zsh"
file "$build/output/zsh"
