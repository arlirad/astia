#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BASE_INITRD OPENSSH_BUILD OUTPUT_INITRD" >&2
    exit 2
fi

base=$1
openssh_build=$2
output=$3
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cpio=${ASTIA_CPIO:-cpio}
stage=$(mktemp -d /tmp/astia-openssh-initrd.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM

command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}
test -f "$base" || {
    echo "openssh: base initrd missing: $base" >&2
    exit 1
}
mkdir -p "$(dirname -- "$output")"
(cd "$stage" && "$cpio" -id --quiet --no-absolute-filenames) <"$base"

for binary in ssh sshd sshd-session ssh-keygen; do
    test -f "$openssh_build/output/$binary" || {
        echo "openssh: built binary missing: $openssh_build/output/$binary" >&2
        exit 1
    }
done
test -f "$openssh_build/etc/sshd_config" || {
    echo "openssh: built sshd_config missing: $openssh_build/etc/sshd_config" >&2
    exit 1
}

mkdir -p "$stage/usr/bin" "$stage/usr/sbin" "$stage/usr/libexec" \
    "$stage/etc/ssh" "$stage/var/empty" "$stage/run"
cp "$openssh_build/output/ssh" "$stage/usr/bin/ssh"
cp "$openssh_build/output/ssh-keygen" "$stage/usr/bin/ssh-keygen"
cp "$openssh_build/output/sshd" "$stage/usr/sbin/sshd"
cp "$openssh_build/output/sshd-session" "$stage/usr/libexec/sshd-session"
cp "$openssh_build/etc/sshd_config" "$stage/etc/ssh/sshd_config"
chmod 0755 "$stage/usr/bin/ssh" "$stage/usr/bin/ssh-keygen" \
    "$stage/usr/sbin/sshd" "$stage/usr/libexec/sshd-session"
chmod 0644 "$stage/etc/ssh/sshd_config"
chmod 0755 "$stage/var/empty" "$stage/run"

libc_so=${OPENSSH_LIBC_SO:-}
if [ -z "$libc_so" ] && [ -f "$openssh_build/astia/image/libc.so" ]; then
    libc_so=$openssh_build/astia/image/libc.so
fi
if [ -z "$libc_so" ] && [ -f "$openssh_build/sysroot/lib/libc.so" ]; then
    libc_so=$openssh_build/sysroot/lib/libc.so
fi
if [ -n "$libc_so" ]; then
    test -f "$libc_so" || {
        echo "openssh: libc.so missing: $libc_so" >&2
        exit 1
    }
    mkdir -p "$stage/lib"
    cp "$libc_so" "$stage/lib/libc.so"
    chmod 0755 "$stage/lib/libc.so"
fi

if [ -n "${OPENSSH_SHELL_BINARY:-}" ]; then
    test -f "$OPENSSH_SHELL_BINARY" || {
        echo "openssh: guest shell missing: $OPENSSH_SHELL_BINARY" >&2
        exit 1
    }
    mkdir -p "$stage/bin"
    cp "$OPENSSH_SHELL_BINARY" "$stage/bin/zsh"
    chmod 0755 "$stage/bin/zsh"
fi

if [ -n "${OPENSSH_TEST_INIT:-}" ]; then
    test -f "$OPENSSH_TEST_INIT" || {
        echo "openssh: test init missing: $OPENSSH_TEST_INIT" >&2
        exit 1
    }
    mkdir -p "$stage/sbin"
    cp "$OPENSSH_TEST_INIT" "$stage/sbin/init"
    chmod 0755 "$stage/sbin/init"
fi

key_dir=${OPENSSH_TEST_KEY_DIR:-$openssh_build/test}
mkdir -p "$key_dir"
host_key=${OPENSSH_HOST_KEY:-$key_dir/ssh_host_ed25519_key}
client_key=${OPENSSH_CLIENT_KEY:-$key_dir/client_key}
authorized_keys=${OPENSSH_AUTHORIZED_KEYS:-$key_dir/authorized_keys}

generate_key() {
    key=$1
    if [ ! -f "$key" ]; then
        command -v ssh-keygen >/dev/null 2>&1 || {
            echo "openssh: host ssh-keygen is required to generate test keys" >&2
            exit 1
        }
        ssh-keygen -q -t ed25519 -N '' -f "$key"
    fi
    if [ ! -f "$key.pub" ]; then
        ssh-keygen -y -f "$key" >"$key.pub"
    fi
}

generate_key "$host_key"
generate_key "$client_key"
if [ ! -f "$authorized_keys" ]; then
    cp "$client_key.pub" "$authorized_keys"
fi

cp "$host_key" "$stage/etc/ssh/ssh_host_ed25519_key"
cp "$host_key.pub" "$stage/etc/ssh/ssh_host_ed25519_key.pub"
cp "$client_key" "$stage/etc/ssh/test_client_key"
cp "$client_key.pub" "$stage/etc/ssh/test_client_key.pub"
cp "$authorized_keys" "$stage/etc/ssh/authorized_keys"
chmod 0600 "$stage/etc/ssh/ssh_host_ed25519_key" \
    "$stage/etc/ssh/test_client_key"
chmod 0644 "$stage/etc/ssh/ssh_host_ed25519_key.pub" \
    "$stage/etc/ssh/test_client_key.pub" "$stage/etc/ssh/authorized_keys"

passwd_file=$stage/etc/passwd
group_file=$stage/etc/group
mkdir -p "$stage/etc"
touch "$passwd_file" "$group_file"
if ! grep -q '^user:' "$passwd_file"; then
    printf '%s\n' 'user::1000:1000:User:/home/user:/bin/zsh' >>"$passwd_file"
fi
if ! grep -q '^sshd:' "$passwd_file"; then
    printf '%s\n' 'sshd:!:74:74:OpenSSH privilege separation:/var/empty:/bin/false' \
        >>"$passwd_file"
fi
if ! grep -q '^users:' "$group_file"; then
    printf '%s\n' 'users::1000:user' >>"$group_file"
fi
if ! grep -q '^sshd:' "$group_file"; then
    printf '%s\n' 'sshd:!:74:' >>"$group_file"
fi

temporary=$output.$$
trap 'rm -rf "$stage"; rm -f "$temporary"' EXIT INT TERM
(cd "$stage" && find . -print | "$cpio" -o --format=newc --quiet --owner=0:0) \
    >"$temporary"
mv -f "$temporary" "$output"
printf 'openssh: packaged %s\n' "$output"
