#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zsh_source=${ZSH_SOURCE:-${1:-}}
if [ -z "$zsh_source" ]; then
    echo "usage: ZSH_SOURCE=/path/to/zsh $0" >&2
    exit 2
fi

zsh_source=$(CDPATH= cd -- "$zsh_source" && pwd)
build=${ZSH_BUILD:-$root/build/user/zsh}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
clang=${CLANG:-clang}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
clang_path=$(command -v "$clang")
resource_dir=$("$clang" --target=x86_64-unknown-elf -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "clang compiler builtins not found below $resource_dir" >&2
    exit 1
fi

source_revision=$(
    if git -C "$zsh_source" rev-parse --git-dir >/dev/null 2>&1; then
        {
            git -C "$zsh_source" rev-parse HEAD
            git -C "$zsh_source" diff HEAD --no-ext-diff --binary
            git -C "$zsh_source" ls-files --others --exclude-standard |
                while IFS= read -r source_file; do
                    sha256sum "$zsh_source/$source_file"
                done
        } | sha256sum | awk '{print $1}'
    else
        find "$zsh_source" -type f -not -path "$zsh_source/.git/*" \
            -exec sha256sum {} + | sort | sha256sum | awk '{print $1}'
    fi
)
zsh_build_source=$zsh_source
if [ ! -x "$zsh_source/configure" ]; then
    zsh_build_source=$build/source
    source_stamp="$zsh_build_source/.astia-source-revision"
    if [ ! -f "$source_stamp" ] || [ "$(cat "$source_stamp")" != "$source_revision" ] ||
        [ ! -x "$zsh_build_source/configure" ]; then
        rm -rf "$zsh_build_source"
        mkdir -p "$zsh_build_source"
        cp -a "$zsh_source"/. "$zsh_build_source"/
        (cd "$zsh_build_source" && ./Util/preconfig)
        printf '%s\n' "$source_revision" >"$source_stamp"
    fi
fi

ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
    "$root/toolchain/build-init-libc.sh"

sysroot_content_key=$(
    {
        find "$sysroot/include" -type f -exec sha256sum {} + | sort
        sha256sum "$sysroot/lib/libastia_libc.a"
    } | sha256sum | awk '{print $1}'
)

mkdir -p "$build"
config_compat_dir=${TMPDIR:-/tmp}/astia-zsh-config-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
mkdir -p "$config_compat_dir"
ln -sfn "$sysroot" "$config_compat_dir/sysroot"
ln -sfn "$zsh_build_source" "$config_compat_dir/source"
ln -sfn "$build/astia/image/crt0.o" "$config_compat_dir/crt0.o"
ln -sfn "$sysroot/lib/libastia_libc.a" "$config_compat_dir/libastia_libc.a"
ln -sfn "$builtins" "$config_compat_dir/libclang_rt_builtins.a"
cat > "$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=x86_64-unknown-elf -ffreestanding -fno-builtin \
-fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
-mno-red-zone -nostdinc -isystem "$resource_dir/include" \
-isystem "$config_compat_dir/sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-zsh-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"

cd "$build"
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-zsh.sh" \
            "$root/toolchain/build-init-libc.sh" \
            "$root/toolchain/astia-x86_64-clang.cmake" "$clang_path"
        "$clang" --version
    } | sha256sum | awk '{print $1}'
)
cat > "$build/config.site" <<'EOF'
# Astia supplies the freestanding libc/kernel ABI.  These answers prevent
# configure's cross-build probes from selecting host-only implementations.
ac_cv_func_lstat=yes
ac_cv_func_wait3=no
ac_cv_func_waitpid=yes
ac_cv_func_sigsuspend=yes
ac_cv_func_sigaction=yes
ac_cv_func_fork=yes
ac_cv_func_setpgid=yes
ac_cv_func_setsid=yes
ac_cv_func_kill=yes
ac_cv_func_strptime=yes
EOF
config_key=$(printf '%s\n' "$zsh_build_source" "$source_revision" \
    "$toolchain_revision" "$sysroot" "$sysroot_content_key" "$builtins" \
    "$compiler_launcher" "$config_compat_dir" |
    sha256sum | awk '{print $1}')
if [ ! -f "$build/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    {
        if [ -f "$build/config.modules" ]; then
            grep -v '^name=zsh/stat ' "$build/config.modules" || true
        fi
        printf '%s\n' 'name=zsh/stat modfile=Src/Modules/stat.mdd link=static load=no'
    } > "$build/config.modules.tmp"
    mv "$build/config.modules.tmp" "$build/config.modules"
    CONFIG_SITE="$build/config.site" \
        "$config_compat_dir/source/configure" \
    --host=x86_64-unknown-elf \
    --build=x86_64-pc-linux-gnu \
    CC="$compiler_launcher" \
    CFLAGS='-O0' \
    LDFLAGS='-nostdlib -static -Wl,-no-pie -Wl,-Ttext-segment=0x40000000' \
        LIBS="$config_compat_dir/crt0.o $config_compat_dir/libastia_libc.a $config_compat_dir/libclang_rt_builtins.a"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

# zsh links the libc archive statically and make does not track it as a
# prerequisite, so an ABI update would otherwise leave the stale binary.
libc_archive="$build/astia/libc/libastia_libc.a"
if [ -e "$libc_archive" ] && [ -e "$build/Src/zsh" ] &&
    [ "$libc_archive" -nt "$build/Src/zsh" ]; then
    rm -f "$build/Src/zsh" "$build/output/zsh"
fi
make -C "$build" -j "$jobs"
mkdir -p "$build/output"
if [ ! -e "$build/output/zsh" ] || ! cmp -s "$build/Src/zsh" "$build/output/zsh"; then
    cp "$build/Src/zsh" "$build/output/zsh"
fi
file "$build/output/zsh"
