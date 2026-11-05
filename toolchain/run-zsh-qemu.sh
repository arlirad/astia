#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_build=${ZSH_BUILD:-/tmp/astia-zsh-build}
zsh_source=${ZSH_SOURCE:-$zsh_build/source}
kernel_build=$root/kernel/build-tests
kernel=${ASTIA_KERNEL:-$kernel_build/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$kernel_build/tests/fixtures/boot_initrd.tar}
work=${ASTIA_ZSH_RUN:-/tmp/astia-zsh-run}
clean_build=${ASTIA_CLEAN_BUILD:-0}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=x86_64-unknown-elf
resource_dir=$($clang --target=$target -print-resource-dir)
sysroot=$zsh_build/sysroot

case "$clean_build" in
    0|1) ;;
    *)
        echo "ASTIA_CLEAN_BUILD must be 0 or 1" >&2
        exit 2
        ;;
esac

command -v "$qemu" >/dev/null 2>&1 || {
    echo "QEMU executable not found: $qemu" >&2
    exit 1
}

if [ ! -t 0 ]; then
    echo "error: interactive zsh needs a real terminal; stdin is already EOF" >&2
    echo "run this script directly from a terminal, not through a closed pipe" >&2
    exit 2
fi

if [ "$clean_build" = 1 ]; then
    echo "Cleaning kernel, zsh, and launcher build directories." >&2
    rm -rf "$kernel_build" "$zsh_build" "$work"
fi

echo "Running the incremental kernel preflight." >&2
"$root/kernel/scripts/test.sh" "$kernel_build"

if [ -e "$zsh_source" ] && [ ! -d "$zsh_source" ]; then
    echo "zsh source is not a directory: $zsh_source" >&2
    exit 1
fi
if [ ! -d "$zsh_source" ]; then
    if [ -n "${ZSH_SOURCE:-}" ]; then
        echo "ZSH_SOURCE does not exist: $zsh_source" >&2
        exit 1
    fi
    command -v git >/dev/null 2>&1 || {
        echo "git is required to clone zsh into $zsh_source" >&2
        exit 1
    }
    mkdir -p "$(dirname -- "$zsh_source")"
    echo "Cloning a clean upstream zsh checkout into $zsh_source" >&2
    git clone --depth=1 https://github.com/zsh-users/zsh.git "$zsh_source" || {
        echo "could not clone zsh; set ZSH_SOURCE to a complete checkout" >&2
        exit 1
    }
fi

for source_file in \
    Src/main.c \
    Src/zsh.h \
    Config/version.mk \
    Makefile.in \
    configure.ac \
    Util/preconfig; do
    if [ ! -f "$zsh_source/$source_file" ]; then
        echo "incomplete zsh source: missing $zsh_source/$source_file" >&2
        echo "use a complete clean checkout; do not use a generated or patched source tree" >&2
        exit 1
    fi
done

if [ ! -x "$zsh_source/configure" ]; then
    zsh_build_source=$zsh_build/source-build
    rm -rf "$zsh_build_source"
    mkdir -p "$zsh_build_source"
    cp -a "$zsh_source"/. "$zsh_build_source"/
else
    zsh_build_source=$zsh_source
fi

mkdir -p "$zsh_build"
ZSH_SOURCE="$zsh_build_source" ZSH_BUILD="$zsh_build" "$root/toolchain/build-zsh.sh"

if [ ! -x "$kernel" ]; then
    echo "Astia kernel runtime is missing or not executable: $kernel" >&2
    echo "build it with kernel/scripts/test.sh, or set ASTIA_KERNEL" >&2
    exit 1
fi
if [ ! -f "$base_initrd" ]; then
    echo "Astia initrd is missing: $base_initrd" >&2
    echo "build it with kernel/scripts/test.sh, or set ASTIA_INITRD" >&2
    exit 1
fi
if ! tar tf "$base_initrd" >/dev/null 2>&1; then
    echo "Astia initrd is not a readable tar archive: $base_initrd" >&2
    echo "rebuild it with kernel/scripts/test.sh, or set ASTIA_INITRD" >&2
    exit 1
fi

rm -rf "$work"
mkdir -p "$work/root/sbin" "$work/root/bin"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/coreutils/build.sh"

tar xf "$base_initrd" -C "$work/root"

common_flags="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
include_flags="-nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

# shellcheck disable=SC2086
$clang $common_flags -fno-builtin $include_flags \
    -c -o "$work/runner.o" "$root/toolchain/zsh-runner.c"
# shellcheck disable=SC2086
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/runner" "$zsh_build/astia/image/crt0.o" "$work/runner.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"

cp "$work/runner" "$work/root/sbin/init"
cp "$zsh_build/output/zsh" "$work/root/bin/zsh"
cp "$work/coreutils/bin/"* "$work/root/bin/"
tar cf "$work/initrd.tar" -C "$work/root" hello_initrd.o sbin/init bin

echo "Starting interactive Astia zsh; exit with Ctrl-D or 'exit'." >&2
exec "$qemu" -m 128M -kernel "$kernel" -initrd "$work/initrd.tar" \
    -serial stdio -display none -monitor none -device isa-debug-exit -no-reboot
