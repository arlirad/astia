#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${OPENSSH_BUILD:-${1:-$root/build/openssh}}
source=${OPENSSH_SOURCE:-$root/user/openssh}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
clang_path=$(command -v "$clang")
openssh_version=V_9_9_P2
openssh_commit=d76b2675116617394cd355a3437b4963a562b64d

if [ ! -d "$source" ]; then
    echo "openssh: source directory does not exist: $source" >&2
    echo "openssh: run toolchain/init-userspace.sh to initialize the pinned source" >&2
    exit 1
fi
source=$(CDPATH= cd -- "$source" && pwd)

if git -C "$source" rev-parse --git-dir >/dev/null 2>&1; then
    source_commit=$(git -C "$source" rev-parse HEAD)
    if [ "$source_commit" != "$openssh_commit" ]; then
        echo "openssh: source is not pinned to $openssh_version ($openssh_commit)" >&2
        echo "openssh: found $source_commit; run toolchain/init-userspace.sh" >&2
        exit 1
    fi
elif ! grep -q 'OpenSSH_9\.9' "$source/version.h" 2>/dev/null; then
    echo "openssh: source is not OpenSSH 9.9 ($openssh_version)" >&2
    exit 1
fi

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

openssh_source=$source
if [ ! -x "$source/configure" ]; then
    openssh_source=$build/source
    source_stamp="$openssh_source/.astia-source-revision"
    if [ ! -f "$source_stamp" ] || [ "$(cat "$source_stamp")" != "$source_revision" ] ||
        [ ! -x "$openssh_source/configure" ]; then
        command -v autoreconf >/dev/null 2>&1 || {
            echo "openssh: autoreconf is required to bootstrap the source" >&2
            exit 1
        }
        rm -rf "$openssh_source"
        mkdir -p "$openssh_source"
        cp -a "$source"/. "$openssh_source"/
        (cd "$openssh_source" && autoreconf -fi)
        printf '%s\n' "$source_revision" >"$source_stamp"
    fi
fi

if [ ! -f "$sysroot/lib/libastia_libc.a" ] || [ ! -f "$crt0" ]; then
    ASTIA_CROSS_BUILD="$build/astia" ASTIA_SYSROOT="$sysroot" CLANG="$clang" \
        "$root/toolchain/build-init-libc.sh"
fi

if ! ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" \
    "$root/toolchain/openssh/check-prerequisites.sh" "$sysroot"; then
    exit 3
fi

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "openssh: clang compiler builtins not found below $resource_dir" >&2
    exit 1
fi

mkdir -p "$build/etc" "$build/output"
config_compat_dir=${TMPDIR:-/tmp}/astia-openssh-config-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
mkdir -p "$config_compat_dir"
ln -sfn "$sysroot" "$config_compat_dir/sysroot"
ln -sfn "$openssh_source" "$config_compat_dir/source"
ln -sfn "$crt0" "$config_compat_dir/crt0.o"
ln -sfn "$builtins" "$config_compat_dir/libclang_rt.builtins.a"
ln -sfn "$sysroot/lib/libastia_libc.a" "$config_compat_dir/libastia_libc.a"
ln -sfn "$sysroot/lib/libc.so" "$config_compat_dir/libc.so"
for stub in crypt util resolv nsl dl pthread; do
    ar rcs "$config_compat_dir/lib$stub.a"
done
cat > "$build/astia-cc" <<EOF
#!/bin/sh
exec $clang --target=$target -ffreestanding -fno-builtin -fPIE \\
-fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc \\
-isystem "$resource_dir/include" -isystem "$config_compat_dir/sysroot/include" "\$@"
EOF
chmod +x "$build/astia-cc"
compiler_launcher=${TMPDIR:-/tmp}/astia-openssh-cc-$(printf '%s' "$build" |
    sha256sum | awk '{print $1}')
ln -sfn "$build/astia-cc" "$compiler_launcher"

cat > "$build/config.site" <<'EOF'
# Astia supplies the freestanding libc/kernel ABI.  These answers replace
# configure's host-run probes with explicit cross-build results.
ac_cv_c_bigendian=no
ac_cv_func_arc4random=yes
ac_cv_func_arc4random_buf=yes
ac_cv_func_arc4random_uniform=yes
ac_cv_func_clock_gettime=yes
ac_cv_func_endgrent=yes
ac_cv_func_fchmod=yes
ac_cv_func_fchown=yes
ac_cv_func_fork=yes
ac_cv_func_futimes=yes
ac_cv_func_getaddrinfo=yes
ac_cv_func_getentropy=yes
ac_cv_func_getgrnam_r=yes
ac_cv_func_getgroups=yes
ac_cv_func_getnameinfo=yes
ac_cv_func_getpeereid=no
ac_cv_func_getpwnam_r=yes
ac_cv_func_getrlimit=yes
ac_cv_func_getspnam=no
ac_cv_func_gettimeofday=yes
ac_cv_func_gai_strerror=yes
ac_cv_func_initgroups=yes
ac_cv_func_inet_ntop=yes
ac_cv_func_inet_pton=yes
ac_cv_func_lchown=yes
ac_cv_func_mblen=yes
ac_cv_func_mkdtemp=yes
ac_cv_func_openlog=yes
ac_cv_func_readpassphrase=no
ac_cv_func_realpath=yes
ac_cv_func_setgroups=yes
ac_cv_func_setproctitle=no
ac_cv_func_setresgid=yes
ac_cv_func_setresuid=yes
ac_cv_func_setrlimit=yes
ac_cv_func_sigaction=yes
ac_cv_func_snprintf=yes
ac_cv_func_strsep=yes
ac_cv_func_strtoll=yes
ac_cv_func_strtoull=yes
ac_cv_func_syslog=yes
ac_cv_func_truncate=yes
ac_cv_func_utimes=yes
ac_cv_func_vsnprintf=yes
ac_cv_header_security_pam_appl_h=no
ac_cv_header_shadow_h=no
ac_cv_header_utmpx_h=no
ac_cv_header_zlib_h=no
ac_cv_lib_crypt_crypt=no
ac_cv_lib_util_openpty=no
EOF

cd "$build"
toolchain_revision=$(
    {
        sha256sum "$root/toolchain/build-openssh.sh" \
            "$root/toolchain/build-init-libc.sh" \
            "$root/toolchain/astia-x86_64-clang.cmake" "$clang_path"
        find "$root/toolchain/openssh" -type f -exec sha256sum {} +
        "$clang" --version
    } | sha256sum | awk '{print $1}'
)
config_key=$(printf '%s\n' "$openssh_source" "$source_revision" \
    "$toolchain_revision" "$sysroot" "$crt0" "$builtins" \
    "$compiler_launcher" "$config_compat_dir" |
    sha256sum | awk '{print $1}')
if [ ! -f "$build/Makefile" ] || [ ! -f "$build/.astia-config-key" ] ||
    [ "$(cat "$build/.astia-config-key")" != "$config_key" ]; then
    CONFIG_SITE="$build/config.site" \
        "$config_compat_dir/source/configure" \
        --host="$target" \
        --build=x86_64-pc-linux-gnu \
        --prefix=/usr \
        --bindir=/usr/bin \
        --sbindir=/usr/sbin \
        --libexecdir=/usr/libexec \
        --sysconfdir=/etc/ssh \
        --with-privsep-user=sshd \
        --with-privsep-path=/var/empty \
        --without-openssl \
        --without-zlib \
        --without-pam \
        --without-shadow \
        --without-selinux \
        --without-audit \
        --without-libedit \
        --without-kerberos5 \
        --without-rpath \
        --disable-utmp \
        --disable-utmpx \
        --disable-wtmp \
        --disable-wtmpx \
        --disable-lastlog \
        --disable-libutil \
        --disable-pututline \
        --disable-pututxline \
        --disable-pkcs11 \
        --disable-security-key \
        --disable-etc-default-login \
        --disable-strip \
        CC="$compiler_launcher" \
        CFLAGS='-O0' \
        LDFLAGS="-nostdlib -Wl,-Bdynamic -Wl,-pie -Wl,-e,_start -Wl,--dynamic-linker=/lib/ld.astia.so -Wl,-rpath-link,$sysroot/lib -L$config_compat_dir" \
        LIBS="$config_compat_dir/crt0.o $config_compat_dir/libclang_rt.builtins.a $sysroot/lib/libc.so"
    printf '%s\n' "$config_key" >"$build/.astia-config-key"
fi

make -C "$build" -j "$jobs" ssh sshd sshd-session ssh-keygen

for binary in ssh sshd sshd-session ssh-keygen; do
    if [ ! -f "$build/$binary" ]; then
        echo "openssh: expected build output missing: $build/$binary" >&2
        exit 1
    fi
    cp "$build/$binary" "$build/output/$binary"
done
cp "$root/toolchain/openssh/sshd_config" "$build/etc/sshd_config"

file "$build/output/ssh" "$build/output/sshd" "$build/output/sshd-session" \
    "$build/output/ssh-keygen"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$build/output/sshd" | grep -q 'NEEDED.*libc\.so' || {
        echo "openssh: sshd is not linked against libc.so" >&2
        exit 1
    }
    readelf -l "$build/output/sshd" | grep -q '/lib/ld.astia.so' || {
        echo "openssh: sshd does not carry /lib/ld.astia.so as its interpreter" >&2
        exit 1
    }
fi
