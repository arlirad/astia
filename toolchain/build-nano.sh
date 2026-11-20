#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${NANO_BUILD:-$root/build/userspace/nano}
source=${NANO_SOURCE:-$root/userspace/nano}
gnulib_source=${NANO_GNULIB_SOURCE:-}
gnulib_revision=f05b5a23f6cef8833402a298d1576a0118912ac8
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

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
            git -C "$source" diff --no-ext-diff --binary
            git -C "$source" ls-files --others --exclude-standard |
                while IFS= read -r source_file; do
                    sha256sum "$source/$source_file"
                done
        } | sha256sum | awk '{print $1}'
    else
        find "$source" -type f -not -path "$source/.git/*" \
            -printf '%P %T@ %s\n' | sort | sha256sum | awk '{print $1}'
    fi
)
bootstrap_revision=
if [ ! -x "$source/configure" ]; then
    if [ -z "$gnulib_source" ]; then
        echo "nano: source has no generated configure script" >&2
        echo "set NANO_GNULIB_SOURCE to a clean gnulib checkout at $gnulib_revision" >&2
        exit 1
    fi
    if [ ! -d "$gnulib_source" ]; then
        echo "nano: gnulib source directory does not exist: $gnulib_source" >&2
        exit 1
    fi
    gnulib_source=$(CDPATH= cd -- "$gnulib_source" && pwd)
    bootstrap_revision=$(git -C "$gnulib_source" rev-parse HEAD 2>/dev/null || true)
    if [ "$bootstrap_revision" != "$gnulib_revision" ]; then
        echo "nano: gnulib source is not pinned to $gnulib_revision" >&2
        exit 1
    fi
fi
source_stamp="$build/source/.astia-source-revision"
source_stamp_key="$source_revision:$bootstrap_revision"
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

resource_dir=$($clang --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "nano: clang compiler builtins not found" >&2
    exit 1
fi

mkdir -p "$build/astia"
cat > "$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=$target -U__linux__ -ffreestanding -fno-builtin -fno-pic -fno-pie \\
-fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \\
-isystem "$resource_dir/include" -isystem "$sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"

$clang --target="$target" -ffreestanding -fno-builtin -fno-pic -fno-pie \
    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \
    -isystem "$resource_dir/include" -isystem "$sysroot/include" \
    -I"$root/toolchain/nano-astia" -c "$root/toolchain/nano-astia/curses.c" \
    -o "$build/astia/curses.o"
rm -f "$build/astia/libncurses.a"
ar rcs "$build/astia/libncurses.a" "$build/astia/curses.o"

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
config_key=$(printf '%s\n' "$build/source" "$source_revision" "$bootstrap_revision" "$clang" "$sysroot" "$crt0" "$builtins" | sha256sum | awk '{print $1}')
if [ ! -f "$build/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    CONFIG_SITE="$build/config.site" PKG_CONFIG=/bin/false NCURSESW_CONFIG=no \
        "$build/source/configure" --host="$target" --build=x86_64-pc-linux-gnu \
    --disable-nls --disable-utf8 --disable-color --disable-mouse \
    --disable-browser --disable-multibuffer --disable-libmagic --enable-tiny \
    CC="$build/astia-cc" CPPFLAGS="-I$root/toolchain/nano-astia -include $root/toolchain/nano-astia/force-assert.h" CFLAGS='-O0' \
    LDFLAGS="-nostdlib -static -Wl,-no-pie -Wl,-Ttext-segment=0x40000000 -L$build/astia" \
        LIBS="$crt0 $builtins -lncurses $sysroot/lib/libastia_libc.a"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

make -j "$jobs"
mkdir -p "$build/output"
if [ ! -e "$build/output/nano" ] || ! cmp -s "$build/src/nano" "$build/output/nano"; then
    cp "$build/src/nano" "$build/output/nano"
fi
file "$build/output/nano"
