#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_build=${ZSH_BUILD:-$root/build/user/zsh}
zsh_source=${ZSH_SOURCE:-$root/user/zsh}
kernel_build=${ASTIA_TEST_BUILD:-$root/kernel/build-tests}
kernel=${ASTIA_KERNEL:-$kernel_build/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$kernel_build/tests/fixtures/boot_initrd.cpio}
work=${ASTIA_ZSH_RUN:-/tmp/astia-zsh-run}
clean_build=${ASTIA_CLEAN_BUILD:-0}
harness=${ASTIA_ZSH_HARNESS:-0}
line_reader=${ASTIA_LINE_READER:-0}
nano_binary=${ASTIA_NANO_BINARY:-}
nano_enabled=${ASTIA_NANO:-0}
nano_harness=${ASTIA_NANO_HARNESS:-0}
nano_iterations=${ASTIA_NANO_ITERATIONS:-3}
curl_binary=${ASTIA_CURL_BINARY:-}
curl_enabled=${ASTIA_CURL:-0}
wget_binary=${ASTIA_WGET_BINARY:-}
wget_enabled=${ASTIA_WGET:-0}
build_only=${ASTIA_ZSH_BUILD_ONLY:-0}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
cpio=${ASTIA_CPIO:-cpio}
target=x86_64-unknown-elf
resource_dir=$("$clang" --target="$target" -print-resource-dir)
sysroot=$zsh_build/sysroot

case "$work" in
    ''|/)
        echo "ASTIA_ZSH_RUN must identify a dedicated working directory" >&2
        exit 2
        ;;
esac
work_base=$(basename -- "$work")
case "$work_base" in
    ''|.|..)
        echo "ASTIA_ZSH_RUN must identify a dedicated working directory" >&2
        exit 2
        ;;
esac
work_parent=$(CDPATH= cd -- "$(dirname -- "$work")" 2>/dev/null && pwd -P) || {
    echo "ASTIA_ZSH_RUN parent directory does not exist: $(dirname -- "$work")" >&2
    exit 2
}
work_path=$work_parent/$work_base
if [ -e "$work_path" ]; then
    if [ ! -d "$work_path" ]; then
        echo "ASTIA_ZSH_RUN is not a directory: $work_path" >&2
        exit 2
    fi
    work_path=$(CDPATH= cd -- "$work_path" && pwd -P)
    if [ -e "$work_path/.git" ] || [ -f "$work_path/CMakeCache.txt" ]; then
        echo "refusing to remove a protected work directory: $work_path" >&2
        exit 2
    fi
fi
dev_initrd_build_root=
if [ -n "${ASTIA_DEV_INITRD_RUN:-}" ]; then
    if [ -z "${ASTIA_DEV_INITRD_BUILD_ROOT:-}" ] ||
       [ ! -f "$ASTIA_DEV_INITRD_BUILD_ROOT/CMakeCache.txt" ]; then
        echo "ASTIA_DEV_INITRD_BUILD_ROOT must identify the configured CMake build directory" >&2
        exit 2
    fi
    dev_initrd_build_root=$(CDPATH= cd -- "$ASTIA_DEV_INITRD_BUILD_ROOT" && pwd -P)
    if [ "$work_path" != "$ASTIA_DEV_INITRD_RUN" ] ||
       [ "$work_parent" != "$dev_initrd_build_root" ]; then
        echo "ASTIA_ZSH_RUN does not match the validated ASTIA_DEV_INITRD_RUN" >&2
        exit 2
    fi
fi
case "$root/" in
    "$work_path/"*)
        echo "ASTIA_ZSH_RUN must not contain the source tree: $work_path" >&2
        exit 2
        ;;
esac
case "$work_path/" in
    "$root/"*)
        if [ -z "$dev_initrd_build_root" ]; then
            echo "ASTIA_ZSH_RUN must be outside the source tree: $work_path" >&2
            exit 2
        fi
        ;;
esac

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

case "$nano_enabled" in
    0|1) ;;
    *)
        echo "ASTIA_NANO must be 0 or 1" >&2
        exit 2
        ;;
esac

case "$nano_harness" in
    0|1) ;;
    *)
        echo "ASTIA_NANO_HARNESS must be 0 or 1" >&2
        exit 2
        ;;
esac

case "$curl_enabled" in
    0|1) ;;
    *)
        echo "ASTIA_CURL must be 0 or 1" >&2
        exit 2
        ;;
esac

case "$wget_enabled" in
    0|1) ;;
    *)
        echo "ASTIA_WGET must be 0 or 1" >&2
        exit 2
        ;;
esac

case "$nano_iterations" in
    ''|*[!0-9]*|0)
        echo "ASTIA_NANO_ITERATIONS must be a positive integer" >&2
        exit 2
        ;;
esac

case "$build_only" in
    0|1) ;;
    *)
        echo "ASTIA_ZSH_BUILD_ONLY must be 0 or 1" >&2
        exit 2
        ;;
esac

if [ "$build_only" = 0 ]; then
    command -v "$qemu" >/dev/null 2>&1 || {
        echo "QEMU executable not found: $qemu" >&2
        exit 1
    }
fi
command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}

if [ "$clean_build" = 1 ]; then
    echo "Cleaning kernel, zsh, and launcher build directories." >&2
    if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
        rm -rf "$kernel_build"
    fi
    rm -rf "$zsh_build" "$work"
fi

echo "Running the incremental kernel preflight." >&2
if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
    "$root/kernel/scripts/test.sh" "$kernel_build"
fi

if [ ! -d "$zsh_source" ]; then
    if [ -n "${ZSH_SOURCE:-}" ]; then
        echo "ZSH_SOURCE does not exist: $zsh_source" >&2
        exit 1
    fi
    echo "zsh submodule is missing: $zsh_source" >&2
    echo "run: toolchain/init-userspace.sh" >&2
    exit 1
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

mkdir -p "$zsh_build"
ZSH_SOURCE="$zsh_source" ZSH_BUILD="$zsh_build" "$root/toolchain/build-zsh.sh"

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
mkdir -p "$work/root/sbin" "$work/root/bin" "$work/root/etc" "$work/root/mod"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
    COREUTILS_BUILD="$work/coreutils" "$root/utils/coreutils/build.sh"

ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
    SYSUTILS_BUILD="$work/sysutils" "$root/utils/sysutils/build.sh"

if [ -n "$curl_binary" ] || [ "$curl_enabled" = 1 ]; then
    if [ -z "$curl_binary" ]; then
        curl_build=${ASTIA_CURL_BUILD:-$root/build/user/curl}
        if ! ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
            CURL_BUILD="$curl_build" "$root/toolchain/build-curl.sh"; then
            echo "Astia curl build failed in $curl_build." >&2
            echo "Initialize userspace sources with toolchain/init-userspace.sh." >&2
            exit 1
        fi
        curl_binary=$curl_build/output/curl
    fi
    if [ ! -x "$curl_binary" ]; then
        echo "Astia curl binary is not executable: $curl_binary" >&2
        exit 1
    fi
fi

if [ -n "$wget_binary" ] || [ "$wget_enabled" = 1 ]; then
    if [ -z "$wget_binary" ]; then
        wget_build=${ASTIA_WGET_BUILD:-$root/build/user/wget}
        if ! ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
            WGET_BUILD="$wget_build" "$root/toolchain/build-wget.sh"; then
            echo "Astia wget build failed in $wget_build." >&2
            echo "Initialize userspace sources with toolchain/init-userspace.sh." >&2
            exit 1
        fi
        wget_binary=$wget_build/output/wget
    fi
    if [ ! -x "$wget_binary" ]; then
        echo "Astia wget binary is not executable: $wget_binary" >&2
        exit 1
    fi
fi

if [ -n "$nano_binary" ] || [ "$nano_enabled" = 1 ]; then
    if [ -z "$nano_binary" ]; then
        nano_build=${ASTIA_NANO_BUILD:-$root/build/user/nano}
        if ! ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$zsh_build/astia/image/crt0.o" \
            NANO_BUILD="$nano_build" NANO_SOURCE="${NANO_SOURCE:-$root/user/nano}" \
            "$root/toolchain/build-nano.sh"; then
            echo "Astia nano build failed in $nano_build." >&2
            echo "Initialize userspace sources with toolchain/init-userspace.sh or set NANO_SOURCE to a complete checkout." >&2
            exit 1
        fi
        nano_binary=$nano_build/output/nano
    fi
    if [ ! -x "$nano_binary" ]; then
        echo "Astia nano binary is not executable: $nano_binary" >&2
        exit 1
    fi
fi

(cd "$work/root" && "$cpio" -id --quiet --no-absolute-filenames) <"$base_initrd"

if [ -n "${ASTIA_DEV_INITRD_MODULES:-}" ]; then
    mkdir -p "$work/root/mod"
    old_ifs=$IFS
    IFS=';'
    for module in $ASTIA_DEV_INITRD_MODULES; do
        [ -n "$module" ] || continue
        cp "$module" "$work/root/mod/"
    done
    IFS=$old_ifs
fi

compile_flags() {
    "$clang" --target="$target" -ffreestanding -fno-pic -fno-pie \
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone "$@"
}

compile_flags -fno-builtin -nostdinc \
    -isystem "$resource_dir/include" -isystem "$sysroot/include" \
    -c -o "$work/runner.o" "$root/toolchain/zsh-runner.c"
compile_flags -fno-builtin -nostdinc \
    -isystem "$resource_dir/include" -isystem "$sysroot/include" \
    -c -o "$work/line-reader.o" "$root/toolchain/line-reader.c"
compile_flags -fno-builtin -nostdinc \
    -isystem "$resource_dir/include" -isystem "$sysroot/include" \
    -c -o "$work/line-reader-runner.o" "$root/toolchain/line-reader-runner.c"
compile_flags -fno-builtin -nostdinc \
    -isystem "$resource_dir/include" -isystem "$sysroot/include" \
    -c -o "$work/pty-test.o" "$root/toolchain/pty-test.c"
compile_flags -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/runner" "$zsh_build/astia/image/crt0.o" "$work/runner.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"
compile_flags -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/line-reader" "$zsh_build/astia/image/crt0.o" "$work/line-reader.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"
compile_flags -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/line-reader-runner" "$zsh_build/astia/image/crt0.o" \
    "$work/line-reader-runner.o" "$sysroot/lib/libastia_libc.a" \
    "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"
compile_flags -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/pty-test" "$zsh_build/astia/image/crt0.o" "$work/pty-test.o" \
    "$sysroot/lib/libastia_libc.a" "$resource_dir/lib/linux/libclang_rt.builtins-x86_64.a"

for runtime in "$work/root/lib/libc.so" "$work/root/lib/ld.astia.so"; do
    if [ ! -f "$runtime" ]; then
        echo "dev initrd is missing the dynamic runtime: $runtime" >&2
        echo "rebuild the base initrd so it stages /lib" >&2
        exit 1
    fi
done

cp "$work/runner" "$work/root/sbin/init"
if [ "$line_reader" = 1 ]; then
    cp "$work/line-reader-runner" "$work/root/sbin/init"
fi
cp "$zsh_build/output/zsh" "$work/root/bin/zsh"
cp "$work/line-reader" "$work/root/bin/line-reader"
cp "$work/pty-test" "$work/root/bin/pty-test"
cp "$work/coreutils/bin/"* "$work/root/bin/"
cp "$work/sysutils/bin/"* "$work/root/bin/"
if [ -n "$nano_binary" ]; then
    cp "$nano_binary" "$work/root/bin/nano"
fi
if [ -n "$curl_binary" ]; then
    cp "$curl_binary" "$work/root/bin/curl"
fi
if [ -n "$wget_binary" ]; then
    cp "$wget_binary" "$work/root/bin/wget"
fi
cat > "$work/root/etc/zshenv" <<'EOF'
PS1='ASTIA_READY> '
EOF
test -x "$work/root/sbin/init"
test -x "$work/root/bin/zsh"
test -f "$work/root/etc/zshenv"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$work/root/bin/zsh" | grep -q 'NEEDED.*libc\.so' || {
        echo "dev initrd zsh is not linked against libc.so" >&2
        exit 1
    }
fi
(cd "$work/root" && find mod sbin bin etc lib -print |
    "$cpio" -o --format=newc --quiet) >"$work/initrd.cpio"

if [ "$build_only" = 1 ]; then
    printf 'Astia zsh/nano initrd: %s\n' "$work/initrd.cpio"
    exit 0
fi

if [ "$harness" = 1 ]; then
    command -v python3 >/dev/null 2>&1 || {
        echo "python3 is required for the zsh PTY harness" >&2
        exit 1
    }
    echo "Starting Astia zsh PTY harness (ASTIA_ZSH_HARNESS=1)." >&2
    if [ "$nano_harness" = 1 ]; then
        if [ -z "$nano_binary" ]; then
            echo "ASTIA_NANO_HARNESS=1 requires ASTIA_NANO=1 or ASTIA_NANO_BINARY." >&2
            exit 1
        fi
        exec python3 "$root/toolchain/zsh-pty-harness.py" \
            --qemu "$qemu" --kernel "$kernel" --initrd "$work/initrd.cpio" \
            --trace "$work/pty.trace" --nano --nano-iterations "$nano_iterations"
    fi
    exec python3 "$root/toolchain/zsh-pty-harness.py" \
        --qemu "$qemu" --kernel "$kernel" --initrd "$work/initrd.cpio" \
        --trace "$work/pty.trace"
fi

echo "Starting Astia zsh interactive session; type exit to stop QEMU." >&2
exec "$qemu" -m 128M -kernel "$kernel" -initrd "$work/initrd.cpio" \
    -serial stdio -display none -monitor none -no-reboot
