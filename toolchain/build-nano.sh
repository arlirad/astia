#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=${NANO_VERSION:-8.6}
url=${NANO_URL:-https://ftp.gnu.org/gnu/nano/nano-$version.tar.xz}
sha256=${NANO_SHA256:-f7abfbf0eed5f573ab51bd77a458f32d82f9859c55e9689f819d96fe1437a619}
build=${NANO_BUILD:-/tmp/astia-nano-build}
source=${NANO_SOURCE:-}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

mkdir -p "$build"
if [ -n "$source" ]; then
    source=$(CDPATH= cd -- "$source" && pwd)
    rm -rf "$build/source"
    mkdir -p "$build/source"
    cp -R "$source"/. "$build/source"/
else
    archive=$build/nano-$version.tar.xz
    if [ ! -f "$archive" ]; then
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL "$url" -o "$archive"
        elif command -v wget >/dev/null 2>&1; then
            wget -q "$url" -O "$archive"
        else
            echo "nano: curl or wget is required to fetch $url" >&2
            exit 1
        fi
    fi
    actual=$(sha256sum "$archive" | awk '{print $1}')
    if [ "$actual" != "$sha256" ]; then
        echo "nano: checksum mismatch for $archive" >&2
        exit 1
    fi
    rm -rf "$build/source"
    mkdir -p "$build/source"
    tar -xf "$archive" -C "$build/source" --strip-components=1
fi

if [ ! -x "$build/source/configure" ]; then
    echo "nano: source has no generated configure script" >&2
    exit 1
fi
patch -d "$build/source" -p1 < "$root/toolchain/nano-astia/gnulib.patch"
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
CONFIG_SITE="$build/config.site" PKG_CONFIG=/bin/false NCURSESW_CONFIG=no \
    "$build/source/configure" --host="$target" --build=x86_64-pc-linux-gnu \
    --disable-nls --disable-utf8 --disable-color --disable-mouse \
    --disable-browser --disable-multibuffer --disable-libmagic --enable-tiny \
    CC="$build/astia-cc" CPPFLAGS="-I$root/toolchain/nano-astia -include $root/toolchain/nano-astia/force-assert.h" CFLAGS='-O0' \
    LDFLAGS="-nostdlib -static -Wl,-no-pie -Wl,-Ttext-segment=0x40000000 -L$build/astia" \
    LIBS="$crt0 $builtins -lncurses $sysroot/lib/libastia_libc.a"

make -j "$jobs"
mkdir -p "$build/output"
cp "$build/src/nano" "$build/output/nano"
file "$build/output/nano"
