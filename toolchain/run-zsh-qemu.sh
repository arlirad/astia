#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_build=${ZSH_BUILD:-/tmp/astia-zsh-build}
zsh_source=${ZSH_SOURCE:-$zsh_build/source}
kernel_build=$root/kernel/build-tests
kernel=${ASTIA_KERNEL:-$kernel_build/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$kernel_build/tests/fixtures/boot_initrd.cpio}
work=${ASTIA_ZSH_RUN:-/tmp/astia-zsh-run}
clean_build=${ASTIA_CLEAN_BUILD:-0}
harness=${ASTIA_ZSH_HARNESS:-0}
line_reader=${ASTIA_LINE_READER:-0}
nano_binary=${ASTIA_NANO_BINARY:-}
nano_enabled=${ASTIA_NANO:-0}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
cpio=${ASTIA_CPIO:-cpio}
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

case "$harness" in
    0|1) ;;
    *)
        echo "ASTIA_ZSH_HARNESS must be 0 or 1" >&2
        exit 2
        ;;
esac

case "$line_reader" in
    0|1) ;;
    *)
        echo "ASTIA_LINE_READER must be 0 or 1" >&2
        exit 2
        ;;
esac

command -v "$qemu" >/dev/null 2>&1 || {
    echo "QEMU executable not found: $qemu" >&2
    exit 1
}
command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}

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
if ! "$cpio" -it --quiet --no-absolute-filenames <"$base_initrd" >/dev/null 2>&1; then
    echo "Astia initrd is not a readable cpio archive: $base_initrd" >&2
    echo "rebuild it with kernel/scripts/test.sh, or set ASTIA_INITRD" >&2
    exit 1
fi

rm -rf "$work"
mkdir -p "$work/root/sbin" "$work/root/bin" "$work/root/etc"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/coreutils/build.sh"

if [ -n "$nano_binary" ] || [ "$nano_enabled" = 1 ]; then
    if [ -z "$nano_binary" ]; then
        nano_build=${ASTIA_NANO_BUILD:-$work/nano}
        ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
            NANO_BUILD="$nano_build" "$root/toolchain/build-nano.sh"
        nano_binary=$nano_build/output/nano
    fi
    if [ ! -x "$nano_binary" ]; then
        echo "Astia nano binary is not executable: $nano_binary" >&2
        exit 1
    fi
fi

(cd "$work/root" && "$cpio" -id --quiet --no-absolute-filenames) <"$base_initrd"

common_flags="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
include_flags="-nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

# shellcheck disable=SC2086
$clang $common_flags -fno-builtin $include_flags \
    -c -o "$work/runner.o" "$root/toolchain/zsh-runner.c"
$clang $common_flags -fno-builtin $include_flags \
    -c -o "$work/line-reader.o" "$root/toolchain/line-reader.c"
$clang $common_flags -fno-builtin $include_flags \
    -c -o "$work/line-reader-runner.o" "$root/toolchain/line-reader-runner.c"
# shellcheck disable=SC2086
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/runner" "$zsh_build/astia/image/crt0.o" "$work/runner.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/line-reader" "$zsh_build/astia/image/crt0.o" "$work/line-reader.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"
$clang $common_flags -nostdlib -nostartfiles -nodefaultlibs -static -no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/line-reader-runner" "$zsh_build/astia/image/crt0.o" \
    "$work/line-reader-runner.o" "$sysroot/lib/libastia_libc.a" \
    "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"

cp "$work/runner" "$work/root/sbin/init"
if [ "$line_reader" = 1 ]; then
    cp "$work/line-reader-runner" "$work/root/sbin/init"
fi
cp "$zsh_build/output/zsh" "$work/root/bin/zsh"
cp "$work/line-reader" "$work/root/bin/line-reader"
cp "$work/coreutils/bin/"* "$work/root/bin/"
if [ -n "$nano_binary" ]; then
    cp "$nano_binary" "$work/root/bin/nano"
fi
cat > "$work/root/etc/zshenv" <<'EOF'
typeset -ga .term.extensions
.term.extensions=(-query)
.term.extensions+=(-integration-prompt -integration-output -integration-pwd -bracketed-paste)
PS1='ASTIA_READY> '
EOF
test -x "$work/root/sbin/init"
test -x "$work/root/bin/zsh"
test -f "$work/root/etc/zshenv"
(cd "$work/root" && find hello_initrd.o sbin bin etc -print |
    "$cpio" -o --format=newc --quiet) >"$work/initrd.cpio"

if [ "$harness" = 1 ]; then
    command -v python3 >/dev/null 2>&1 || {
        echo "python3 is required for the zsh PTY harness" >&2
        exit 1
    }
    echo "Starting Astia zsh PTY harness (ASTIA_ZSH_HARNESS=1)." >&2
    exec python3 "$root/toolchain/zsh-pty-harness.py" \
        --qemu "$qemu" --kernel "$kernel" --initrd "$work/initrd.cpio" \
        --trace "$work/pty.trace"
fi

echo "Starting Astia zsh interactive session; type exit to stop QEMU." >&2
exec "$qemu" -m 128M -kernel "$kernel" -initrd "$work/initrd.cpio" \
    -serial stdio -display none -monitor none -no-reboot
