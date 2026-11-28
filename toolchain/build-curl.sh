#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${CURL_BUILD:-$root/build/user/curl}
source=${CURL_SOURCE:-$root/user/curl}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
curl_version=8.23.0
clang_path=$(command -v "$clang")

if [ ! -x "$source/configure" ]; then
    echo "curl: source directory has no configure script: $source" >&2
    echo "curl: run toolchain/init-userspace.sh to fetch the pinned source" >&2
    exit 1
fi

source=$(CDPATH= cd -- "$source" && pwd)

if ! grep -q "LIBCURL_VERSION \"$curl_version\"" "$source/include/curl/curlver.h"; then
    echo "curl: source is not curl $curl_version" >&2
    exit 1
fi

source_revision=$(
    find "$source" -type f -not -path "$source/.git/*" \
        -exec sha256sum {} + | sort | sha256sum | awk '{print $1}'
)

if [ ! -f "$sysroot/lib/libastia_libc.a" ]; then
    ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
        "$root/toolchain/build-init-libc.sh"
fi
if [ ! -f "$crt0" ]; then
    echo "curl: Astia startup object missing: $crt0" >&2
    exit 1
fi

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "curl: clang compiler builtins not found" >&2
    exit 1
fi

mkdir -p "$build/astia" "$build/output"
config_compat_dir=${TMPDIR:-/tmp}/astia-curl-config-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
mkdir -p "$config_compat_dir"
ln -sfn "$sysroot" "$config_compat_dir/sysroot"
ln -sfn "$source" "$config_compat_dir/source"
ln -sfn "$root/toolchain/astia-ports" "$config_compat_dir/astia-ports"
ln -sfn "$crt0" "$config_compat_dir/crt0.o"
ln -sfn "$builtins" "$config_compat_dir/libclang_rt.builtins.a"
ln -sfn "$sysroot/lib/libastia_libc.a" "$config_compat_dir/libastia_libc.a"
ln -sfn "$sysroot/lib/libc.so" "$config_compat_dir/libc.so"
for stub in resolv nsl dl pthread rt; do
    ar rcs "$config_compat_dir/lib$stub.a"
done

cat >"$build/astia-cc" <<EOF
#!/bin/sh
link=1
for argument in "\$@"; do
    case "\$argument" in
        -c|-E|-S|-M|-MM) link=0 ;;
    esac
done
if [ "\$link" -eq 1 ]; then
    exec $clang --target=$target -D__linux__ -D_POSIX_C_SOURCE=200809L -ffreestanding -fno-builtin -fPIE -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem "$resource_dir/include" -isystem "$config_compat_dir/sysroot/include" -include "$config_compat_dir/astia-ports/errno-macros.h" "\$@" "$config_compat_dir/crt0.o" "$builtins" "$sysroot/lib/libc.so"
else
    exec $clang --target=$target -D__linux__ -D_POSIX_C_SOURCE=200809L -ffreestanding -fno-builtin -fPIE -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem "$resource_dir/include" -isystem "$config_compat_dir/sysroot/include" -include "$config_compat_dir/astia-ports/errno-macros.h" "\$@"
fi
EOF

chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-curl-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"

cat >"$build/config.site" <<'EOF'
# Astia supplies the freestanding libc/kernel ABI.  These answers replace
# configure's host-run probes with explicit cross-build results.
ac_cv_c_bigendian=no
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
ac_cv_func_fork=yes
ac_cv_func_getaddrinfo=yes
ac_cv_func_getnameinfo=yes
ac_cv_func_gettimeofday=yes
ac_cv_func_gai_strerror=yes
ac_cv_func_inet_ntop=yes
ac_cv_func_inet_pton=yes
ac_cv_func_lchown=yes
ac_cv_func_realpath=yes
ac_cv_func_sigaction=yes
ac_cv_func_siginterrupt=yes
ac_cv_func_snprintf=yes
ac_cv_func_strtoll=yes
ac_cv_func_strtoull=yes
ac_cv_func_utimes=yes
ac_cv_func_vsnprintf=yes
ac_cv_header_zlib_h=no
EOF

cd "$build"
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-curl.sh" \
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
        --disable-shared --enable-static \
        --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
        --disable-telnet --disable-tftp --disable-pop3 --disable-imap \
        --disable-smtp --disable-gopher --disable-mqtt --disable-manual \
        --disable-threaded-resolver --disable-ipv6 --disable-alt-svc \
        --disable-hsts --disable-aws-sigv4 \
        --without-ssl --without-libidn2 --without-brotli --without-zstd \
        --without-nghttp2 --without-libpsl --without-ca-bundle \
        --without-ca-path \
        CC="$compiler_launcher" \
        CPPFLAGS="-I$config_compat_dir/sysroot/include" \
        CFLAGS='-O0' \
        LIBS="" \
        LDFLAGS="-nostdlib -Wl,-Bdynamic -Wl,-pie -Wl,-e,_start -Wl,--dynamic-linker=/lib/ld.astia.so -Wl,-rpath-link,$sysroot/lib -L$config_compat_dir"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

if [ -e "$sysroot/lib/libc.so" ] && [ -e "$build/src/curl" ] &&
    [ "$sysroot/lib/libc.so" -nt "$build/src/curl" ]; then
    rm -f "$build/src/curl" "$build/output/curl"
fi

make -j "$jobs"
mkdir -p "$build/output"
if [ ! -e "$build/output/curl" ] || ! cmp -s "$build/src/curl" "$build/output/curl"; then
    cp "$build/src/curl" "$build/output/curl"
fi
file "$build/output/curl"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$build/output/curl" | grep -q 'NEEDED.*libc\.so' || {
        echo "curl is not linked against libc.so" >&2
        exit 1
    }
fi
