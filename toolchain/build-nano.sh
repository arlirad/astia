#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${NANO_BUILD:-$root/build/user/nano}
source=${NANO_SOURCE:-$root/user/nano}
gnulib_source=${NANO_GNULIB_SOURCE:-$root/user/gnulib}
gnulib_revision=f05b5a23f6cef8833402a298d1576a0118912ac8
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
clang_path=$(command -v "$clang")

mkdir -p "$build"
if [ ! -d "$source" ]; then
    echo "nano: source directory does not exist: $source" >&2
    exit 1
fi
source=$(CDPATH= cd -- "$source" && pwd)
source_revision=$(
    if git -C "$source" rev-parse --git-dir >/dev/null 2>&1; then
        {
            git -C "$source" rev-parse HEAD
            git -C "$source" diff HEAD --no-ext-diff --binary
            git -C "$source" ls-files --others --exclude-standard |
                while IFS= read -r source_file; do
                    sha256sum "$source/$source_file"
                done
        } | sha256sum | awk '{print $1}'
    else
        find "$source" -type f -not -path "$source/.git/*" \
            -exec sha256sum {} + | sort | sha256sum | awk '{print $1}'
    fi
)
bootstrap_revision=
if [ ! -x "$source/configure" ]; then
    if [ -z "$gnulib_source" ]; then
        echo "nano: source has no generated configure script" >&2
        echo "run toolchain/init-userspace.sh to initialize gnulib at $gnulib_revision" >&2
        exit 1
    fi
    if [ ! -d "$gnulib_source" ]; then
        echo "nano: gnulib source directory does not exist: $gnulib_source" >&2
        exit 1
    fi
    gnulib_source=$(CDPATH= cd -- "$gnulib_source" && pwd)
    bootstrap_commit=$(git -C "$gnulib_source" rev-parse HEAD 2>/dev/null || true)
    if [ "$bootstrap_commit" != "$gnulib_revision" ]; then
        echo "nano: gnulib source is not pinned to $gnulib_revision" >&2
        exit 1
    fi
    bootstrap_revision=$(
        {
            printf '%s\n' "$bootstrap_commit"
            git -C "$gnulib_source" diff HEAD --no-ext-diff --binary
            git -C "$gnulib_source" ls-files --others --exclude-standard |
                while IFS= read -r source_file; do
                    sha256sum "$gnulib_source/$source_file"
                done
        } | sha256sum | awk '{print $1}'
    )
fi
patch_revision=$(sha256sum "$root/toolchain/nano-astia/gnulib.patch" |
    awk '{print $1}')
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-nano.sh" \
            "$root/toolchain/build-init-libc.sh" "$clang_path"
        find "$root/toolchain/nano-astia" -type f -exec sha256sum {} +
        "$clang" --version
    } | sha256sum | awk '{print $1}'
)
source_stamp="$build/source/.astia-source-revision"
source_stamp_key="$source_revision:$bootstrap_revision:$patch_revision:$toolchain_revision"
if [ ! -f "$source_stamp" ] || [ "$(cat "$source_stamp")" != "$source_stamp_key" ] ||
    [ ! -x "$build/source/configure" ]; then
    rm -rf "$build/source"
    mkdir -p "$build/source"
    cp -R "$source"/. "$build/source"/
    if [ ! -x "$build/source/configure" ]; then
        ln -s "$gnulib_source" "$build/source/gnulib"
        (cd "$build/source" && ./autogen.sh)
    fi
    patch -d "$build/source" -p1 < "$root/toolchain/nano-astia/gnulib.patch"
    find "$build/source" -type f \( -name 'Makefile.in' -o -name 'aclocal.m4' \
        -o -name 'config.h.in' -o -name 'configure' \) -exec touch {} +
    printf '%s\n' "$source_stamp_key" >"$source_stamp"
fi
if [ ! -x "$build/source/configure" ]; then
    echo "nano: source has no generated configure script" >&2
    exit 1
fi

if [ ! -f "$sysroot/lib/libastia_libc.a" ]; then
    ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
        "$root/toolchain/build-init-libc.sh"
fi
if [ ! -f "$crt0" ]; then
    echo "nano: Astia startup object missing: $crt0" >&2
    exit 1
fi

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "nano: clang compiler builtins not found" >&2
    exit 1
fi

mkdir -p "$build/astia"
config_compat_dir=${TMPDIR:-/tmp}/astia-nano-config-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
mkdir -p "$config_compat_dir"
ln -sfn "$sysroot" "$config_compat_dir/sysroot"
ln -sfn "$build/source" "$config_compat_dir/source"
ln -sfn "$build/astia" "$config_compat_dir/astia"
ln -sfn "$root/toolchain/nano-astia" "$config_compat_dir/nano-astia"
ln -sfn "$crt0" "$config_compat_dir/crt0.o"
ln -sfn "$builtins" "$config_compat_dir/libclang_rt.builtins.a"
ln -sfn "$sysroot/lib/libastia_libc.a" "$config_compat_dir/libastia_libc.a"
ln -sfn "$sysroot/lib/libc.so" "$config_compat_dir/libc.so"
for stub in dl m rt tinfo terminfo curses termcap pthread; do
    ar rcs "$config_compat_dir/lib$stub.a"
done
cat > "$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=$target -U__linux__ -ffreestanding -fno-builtin -fPIE \\
-fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \\
-isystem "$resource_dir/include" -isystem "$config_compat_dir/sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-nano-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"
curses_key=$(
    {
        sha256sum "$root/toolchain/nano-astia/curses.c" \
            "$root/toolchain/nano-astia/curses.h" "$sysroot/lib/libastia_libc.a" \
            "$sysroot/lib/libc.so" \
            "$clang_path"
        "$clang" --version
        printf '%s\n' "$target" "$resource_dir" "$sysroot"
    } | sha256sum | awk '{print $1}'
)
if [ ! -f "$build/astia/curses.o" ] ||
    [ ! -f "$build/astia/libncurses.a" ] ||
    [ ! -f "$build/astia/curses.key" ] ||
    [ "$(cat "$build/astia/curses.key")" != "$curses_key" ]; then
    $clang --target="$target" -ffreestanding -fno-builtin -fPIE \
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \
        -isystem "$resource_dir/include" -isystem "$sysroot/include" \
        -I"$root/toolchain/nano-astia" -c "$root/toolchain/nano-astia/curses.c" \
        -o "$build/astia/curses.o"
    ar rcs "$build/astia/libncurses.a" "$build/astia/curses.o"
    printf '%s\n' "$curses_key" >"$build/astia/curses.key"
fi

cat > "$build/config.site" <<'EOF'
ac_cv_header_ncurses_h=no
ac_cv_header_curses_h=yes
ac_cv_lib_ncurses_use_default_colors=no
ac_cv_lib_ncurses_set_escdelay=no
ac_cv_lib_ncurses_key_defined=no
ac_cv_lib_ncursesw_wget_wch=no
ac_cv_header_iconv_h=no
ac_cv_header_libintl_h=no
ac_cv_header_magic_h=no
ac_cv_header_zlib_h=no
ac_cv_func_iconv=no
ac_cv_func_dlopen=no
ac_cv_func_getprogname=yes
gl_cv_onwards_func_getprogname=yes
ac_cv_func_mbsinit=yes
ac_cv_func_clock=yes
gl_cv_func_stat_file_slash=yes
EOF

cd "$build"
config_key=$(printf '%s\n' "$config_compat_dir/source" "$source_revision" \
    "$bootstrap_revision" "$patch_revision" "$toolchain_revision" \
    "$sysroot" "$crt0" "$builtins" "$compiler_launcher" "$config_compat_dir" |
    sha256sum | awk '{print $1}')
if [ ! -f "$build/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    CONFIG_SITE="$build/config.site" PKG_CONFIG=/bin/false NCURSESW_CONFIG=no \
        "$config_compat_dir/source/configure" \
    --host="$target" --build=x86_64-pc-linux-gnu \
    --disable-nls --disable-utf8 --disable-color --disable-mouse \
    --disable-browser --disable-multibuffer --disable-libmagic --enable-tiny \
    CC="$compiler_launcher" \
    CPPFLAGS="-I$config_compat_dir/nano-astia -include $config_compat_dir/nano-astia/force-assert.h" \
    CFLAGS='-O0' \
    LDFLAGS="-nostdlib -Wl,-Bdynamic -Wl,-pie -Wl,-e,_start -Wl,--dynamic-linker=/lib/ld.astia.so -Wl,-rpath-link,$sysroot/lib -L$config_compat_dir/astia -L$config_compat_dir" \
        LIBS="$config_compat_dir/crt0.o $config_compat_dir/libclang_rt.builtins.a -lncurses $sysroot/lib/libc.so"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

# nano links the libc archive statically and make does not track it as a
# prerequisite, so an ABI update would otherwise leave the stale binary.
if { [ -e "$sysroot/lib/libastia_libc.a" ] && [ -e "$build/src/nano" ] &&
    [ "$sysroot/lib/libastia_libc.a" -nt "$build/src/nano" ]; } ||
    { [ -e "$sysroot/lib/libc.so" ] && [ -e "$build/src/nano" ] &&
    [ "$sysroot/lib/libc.so" -nt "$build/src/nano" ]; }; then
    rm -f "$build/src/nano" "$build/output/nano"
fi
make -j "$jobs"
mkdir -p "$build/output"
if [ ! -e "$build/output/nano" ] || ! cmp -s "$build/src/nano" "$build/output/nano"; then
    cp "$build/src/nano" "$build/output/nano"
fi
file "$build/output/nano"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$build/output/nano" | grep -q 'NEEDED.*libc\.so' || {
        echo "nano is not linked against libc.so" >&2
        exit 1
    }
fi
