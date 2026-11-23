#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${NCURSES_BUILD:-$root/build/user/ncurses}
source=${NCURSES_SOURCE:-$root/user/ncurses}
package=${NCURSES_PACKAGE:-$build/package}
cross_build=${ASTIA_CROSS_BUILD:-$build/cross}
sysroot=${ASTIA_SYSROOT:-$cross_build/sysroot}
crt0=${ASTIA_CRT0:-$cross_build/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
expected_revision=0096bd402c4a9c8f39bd7ed266e1b8920327e4d8
clang_path=$(command -v "$clang")

if [ ! -d "$source" ]; then
    echo "ncurses: source directory does not exist: $source" >&2
    exit 1
fi
source=$(CDPATH= cd -- "$source" && pwd)
if ! git -C "$source" rev-parse --git-dir >/dev/null 2>&1; then
    echo "ncurses: source is not a reviewed git checkout: $source" >&2
    exit 1
fi
source_commit=$(git -C "$source" rev-parse HEAD)
if [ "$source_commit" != "$expected_revision" ]; then
    echo "ncurses: source is not pinned to $expected_revision" >&2
    exit 1
fi
if [ -n "$(git -C "$source" status --porcelain)" ]; then
    echo "ncurses: source checkout is dirty: $source" >&2
    exit 1
fi
if [ "$source" = "$root/user/ncurses" ]; then
    gitlink=$(git -C "$root" ls-files --stage -- user/ncurses |
        awk '{print $2}')
    if [ "$gitlink" != "$expected_revision" ]; then
        echo "ncurses: user/ncurses gitlink is not $expected_revision" >&2
        exit 1
    fi
fi

tic_path=$(command -v tic || true)
infocmp_path=$(command -v infocmp || true)
if [ -z "$tic_path" ] || [ -z "$infocmp_path" ]; then
    echo "ncurses: host tic and infocmp are required for the vt220 fallback" >&2
    exit 1
fi

if [ ! -f "$sysroot/lib/libastia_libc.a" ] || [ ! -f "$crt0" ]; then
    ASTIA_CROSS_BUILD="$cross_build" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
        "$root/toolchain/build-init-libc.sh"
fi
if [ ! -f "$sysroot/lib/libastia_libc.a" ] || [ ! -f "$crt0" ]; then
    echo "ncurses: Astia sysroot or startup object is missing" >&2
    exit 1
fi

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtin_lib=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtin_lib" ]; then
    echo "ncurses: clang compiler builtins not found below $resource_dir" >&2
    exit 1
fi

mkdir -p "$build"
cat >"$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=$target -U__linux__ -ffreestanding -fno-builtin -fno-pic \\
-fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \\
-isystem "$resource_dir/include" -isystem "$sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-ncurses-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"

obj=$build/build
stage=$build/stage
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-ncurses.sh" \
            "$root/toolchain/build-init-libc.sh" "$clang_path" \
            "$tic_path" "$infocmp_path"
        "$clang" --version
        tic -V
        printf '%s\n' "$target" "$source_commit" "$expected_revision"
    } | sha256sum | awk '{print $1}'
)
config_key=$(printf '%s\n' "$source" "$source_commit" "$toolchain_revision" \
    "$sysroot" "$crt0" "$builtin_lib" "$compiler_launcher" "$package" |
    sha256sum | awk '{print $1}')
configure_args="--host=$target --build=x86_64-pc-linux-gnu --prefix=/ --libdir=/lib --includedir=/include --without-shared --with-normal --without-debug --without-ada --without-cxx --without-cxx-binding --without-progs --without-tests --without-manpages --disable-db-install --disable-database --with-fallbacks=vt220 --without-dlsym --without-gpm --disable-home-terminfo --disable-root-environ --disable-widec --disable-ext-colors --enable-termcap"

if [ ! -f "$obj/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    rm -rf "$obj"
    mkdir -p "$obj"
    (
        cd "$obj"
        TIC="$tic_path" INFOCMP="$infocmp_path" \
            "$source/configure" $configure_args \
            CC="$compiler_launcher" \
            CFLAGS='-O2' \
            LDFLAGS="-nostdlib -static -Wl,-no-pie -Wl,-Ttext-segment=0x40000000" \
            LIBS="$crt0 $sysroot/lib/libastia_libc.a $builtin_lib"
    )
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

make -C "$obj" -j "$jobs"

rm -rf "$stage"
mkdir -p "$stage"
make -C "$obj" install DESTDIR="$stage"

rm -rf "$package"
mkdir -p "$package/include" "$package/lib"
for header in curses.h ncurses.h term.h unctrl.h ncurses_dll.h term_entry.h; do
    install -m 644 "$stage/include/$header" "$package/include/$header"
done
install -m 644 "$stage/lib/libncurses.a" "$package/lib/libncurses.a"
install -m 644 "$stage/lib/libncurses.a" "$package/lib/libcurses.a"

for required in include/curses.h include/ncurses.h include/term.h \
    include/unctrl.h include/ncurses_dll.h include/term_entry.h \
    lib/libncurses.a lib/libcurses.a; do
    if [ ! -f "$package/$required" ]; then
        echo "ncurses: missing package output: $required" >&2
        exit 1
    fi
done
if [ -e "$package/include/termcap.h" ]; then
    echo "ncurses: package must not own termcap.h" >&2
    exit 1
fi
expected_files=$(printf '%s\n' curses.h ncurses.h term.h unctrl.h \
    ncurses_dll.h term_entry.h | sort)
actual_headers=$(find "$package/include" -maxdepth 1 -type f -exec basename {} \; |
    sort)
if [ "$actual_headers" != "$expected_files" ]; then
    echo "ncurses: unexpected headers in package/include" >&2
    printf '%s\n' "$actual_headers" >&2
    exit 1
fi
expected_libs=$(printf '%s\n' libcurses.a libncurses.a | sort)
actual_libs=$(find "$package/lib" -maxdepth 1 -type f -exec basename {} \; |
    sort)
if [ "$actual_libs" != "$expected_libs" ]; then
    echo "ncurses: unexpected libraries in package/lib" >&2
    printf '%s\n' "$actual_libs" >&2
    exit 1
fi
if ! cmp -s "$package/lib/libncurses.a" "$package/lib/libcurses.a"; then
    echo "ncurses: libcurses.a is not the libncurses.a archive" >&2
    exit 1
fi
if ! nm "$package/lib/libncurses.a" 2>/dev/null | grep -q ' T tgetent$'; then
    echo "ncurses: termcap entry points are missing" >&2
    exit 1
fi
if ! nm "$package/lib/libncurses.a" 2>/dev/null | grep -q ' T _nc_fallback$'; then
    echo "ncurses: vt220 fallback support is missing" >&2
    exit 1
fi
if ! grep -aq 'vt220|vt200|DEC VT220' "$package/lib/libncurses.a"; then
    echo "ncurses: vt220 fallback description is missing" >&2
    exit 1
fi
if find "$package" -iname '*terminfo*' -o -name '*.so' -o -name '*.so.*' |
    grep -q .; then
    echo "ncurses: package contains terminfo or shared-library outputs" >&2
    exit 1
fi
if [ -n "$(git -C "$source" status --porcelain)" ]; then
    echo "ncurses: build modified the pinned source checkout" >&2
    exit 1
fi

file "$package/lib/libncurses.a"
printf '%s\n' "ncurses package: $package"
