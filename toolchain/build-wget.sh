#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${WGET_BUILD:-$root/build/user/wget}
source=${WGET_SOURCE:-$root/user/wget}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
wget_version=1.25.0
clang_path=$(command -v "$clang")

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

if [ ! -x "$source/configure" ]; then
    boot=$build/source
    stamp=$boot/.astia-source-revision
    if [ ! -x "$boot/configure" ] || [ ! -f "$stamp" ] ||
        [ "$(cat "$stamp")" != "$source_revision" ]; then
        gnulib_source=${WGET_GNULIB_SOURCE:-$root/user/gnulib}
        if [ ! -x "$gnulib_source/gnulib-tool" ]; then
            echo "wget: gnulib source is missing: $gnulib_source" >&2
            echo "wget: run toolchain/init-userspace.sh" >&2
            exit 1
        fi
        command -v autopoint >/dev/null 2>&1 || {
            echo "wget: autopoint is required to bootstrap the git checkout" >&2
            exit 1
        }
        rm -rf "$boot"
        mkdir -p "$boot"
        (cd "$source" && tar --exclude=.git -cf - .) | tar -C "$boot" -xf -
        printf '%s\n' "$wget_version" >"$boot/.tarball-version"
        (cd "$boot" && ./bootstrap --gnulib-srcdir="$gnulib_source" --skip-po)
        printf '%s\n' "$source_revision" >"$stamp"
    fi
    source=$boot
fi

if ! grep -q "PACKAGE_VERSION='$wget_version'" "$source/configure"; then
    echo "wget: source is not wget $wget_version" >&2
    exit 1
fi

if [ ! -f "$sysroot/lib/libastia_libc.a" ]; then
    ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
        "$root/toolchain/build-init-libc.sh"
fi
if [ ! -f "$crt0" ]; then
    echo "wget: Astia startup object missing: $crt0" >&2
    exit 1
fi

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "wget: clang compiler builtins not found" >&2
    exit 1
fi

mkdir -p "$build/astia" "$build/output"
config_compat_dir=${TMPDIR:-/tmp}/astia-wget-config-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
mkdir -p "$config_compat_dir"
ln -sfn "$sysroot" "$config_compat_dir/sysroot"
ln -sfn "$source" "$config_compat_dir/source"
ln -sfn "$crt0" "$config_compat_dir/crt0.o"
ln -sfn "$builtins" "$config_compat_dir/libclang_rt.builtins.a"
ln -sfn "$sysroot/lib/libastia_libc.a" "$config_compat_dir/libastia_libc.a"
ln -sfn "$sysroot/lib/libc.so" "$config_compat_dir/libc.so"
ln -sfn "$root/toolchain/astia-ports" "$config_compat_dir/astia-ports"
for stub in resolv nsl dl pthread rt; do
    ar rcs "$config_compat_dir/lib$stub.a"
done

cat >"$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=$target -D__linux__ -D_POSIX_C_SOURCE=200809L -ffreestanding -fno-builtin -fPIE -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem "$resource_dir/include" -isystem "$config_compat_dir/sysroot/include" -include "$config_compat_dir/astia-ports/errno-macros.h" "\$@"
EOF

chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-wget-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"

cat >"$build/config.site" <<'EOF'
# Astia supplies the freestanding libc/kernel ABI.  These answers replace
# configure's host-run probes with explicit cross-build results.
ac_cv_c_bigendian=no
gl_cv_cc_wallow=none
ac_cv_sizeof_int=4
ac_cv_sizeof_long=8
ac_cv_sizeof_off_t=8
ac_cv_sizeof_short=2
ac_cv_sizeof_size_t=8
ac_cv_sizeof_time_t=8
ac_cv_sizeof_voidp=8
ac_cv_sizeof_long_long=8
ac_cv_func_clock_gettime=yes
ac_cv_func_fchmod=yes
ac_cv_func_fchown=yes
ac_cv_func_flock=yes
ac_cv_func_fpurge=yes
ac_cv_func___freading=yes
ac_cv_func___fwriting=yes
ac_cv_func_freading=yes
ac_cv_func_fwriting=yes
ac_cv_func_pthread_sigmask=yes
ac_cv_func_getprogname=yes
ac_cv_func_getaddrinfo=yes
ac_cv_func_getnameinfo=yes
ac_cv_func_gettimeofday=yes
ac_cv_func_gai_strerror=yes
ac_cv_func_inet_ntop=yes
ac_cv_func_inet_pton=yes
ac_cv_func_lchown=yes
ac_cv_func_mbsinit=yes
ac_cv_func_realpath=yes
ac_cv_func_sigaction=yes
ac_cv_func_snprintf=yes
ac_cv_func_strtoll=yes
ac_cv_func_strtoull=yes
ac_cv_func_utimensat=yes
ac_cv_func_utimes=yes
ac_cv_func_vsnprintf=yes
ac_cv_header_iconv_h=no
ac_cv_header_inttypes_h=yes
ac_cv_header_libintl_h=no
ac_cv_header_pcre_h=no
ac_cv_header_pcre2_h=no
ac_cv_header_zlib_h=no
ac_cv_lib_psl_psl_builtin=no
ac_cv_lib_uuid_uuid_generate=no
EOF

cd "$build"
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-wget.sh" \
            "$root/toolchain/build-init-libc.sh" \
            "$root/toolchain/astia-x86_64-clang.cmake" "$clang_path"
        "$clang" --version
    } | sha256sum | awk '{print $1}'
)
config_key=$(printf '%s\n' "$source" "$source_revision" "$toolchain_revision" \
    "$sysroot" "$crt0" "$builtins" "$compiler_launcher" "$config_compat_dir" |
    sha256sum | awk '{print $1}')
if [ ! -f "$build/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    CONFIG_SITE="$build/config.site" PKG_CONFIG=/bin/false \
        "$source/configure" \
        --host="$target" --build=x86_64-pc-linux-gnu \
        --disable-nls --disable-iri --disable-pcre --disable-pcre2 \
        --disable-xattr --disable-opie \
        --disable-ntlm --without-libpsl --without-zlib --without-libuuid \
        --with-ssl=no \
        CC="$compiler_launcher" \
        CPPFLAGS="-I$config_compat_dir/sysroot/include" \
        CFLAGS='-O0' \
        LDFLAGS="-nostdlib -Wl,-Bdynamic -Wl,-pie -Wl,-e,_start -Wl,--dynamic-linker=/lib/ld.astia.so -Wl,-rpath-link,$sysroot/lib -L$config_compat_dir" \
        LIBS="$config_compat_dir/crt0.o $builtins $sysroot/lib/libc.so"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

if [ -e "$sysroot/lib/libc.so" ] && [ -e "$build/src/wget" ] &&
    [ "$sysroot/lib/libc.so" -nt "$build/src/wget" ]; then
    rm -f "$build/src/wget" "$build/output/wget"
fi

make -C lib -j "$jobs"
make -C src -j "$jobs"
mkdir -p "$build/output"
if [ ! -e "$build/output/wget" ] || ! cmp -s "$build/src/wget" "$build/output/wget"; then
    cp "$build/src/wget" "$build/output/wget"
fi
file "$build/output/wget"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$build/output/wget" | grep -q 'NEEDED.*libc\.so' || {
        echo "wget is not linked against libc.so" >&2
        exit 1
    }
fi
