#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
openssh_commit=d76b2675116617394cd355a3437b4963a562b64d
tmp=$(mktemp -d /tmp/astia-openssh-check.XXXXXX)
trap 'rm -rf "$tmp"' EXIT INT TERM

fail() {
    echo "openssh port check: $1" >&2
    exit 1
}

for script in \
    "$root/toolchain/build-openssh.sh" \
    "$root/toolchain/package-openssh-initrd.sh" \
    "$root/toolchain/openssh/check-prerequisites.sh" \
    "$root/toolchain/openssh/run-qemu.sh"; do
    test -f "$script" || fail "missing script: $script"
    sh -n "$script" || fail "syntax error: $script"
done

build_script=$root/toolchain/build-openssh.sh
grep -q -- '--without-openssl' "$build_script" ||
    fail "build script lost --without-openssl"
grep -q -- '--without-zlib' "$build_script" ||
    fail "build script lost --without-zlib"
grep -q -- '--without-shadow' "$build_script" ||
    fail "build script lost --without-shadow"
grep -q -- '--with-privsep-user=sshd' "$build_script" ||
    fail "build script lost the privilege-separation user"
grep -q -- '--with-privsep-path=/var/empty' "$build_script" ||
    fail "build script lost the privilege-separation path"
grep -q 'sshd-session' "$build_script" ||
    fail "build script does not build the sshd-session helper"
grep -q 'check-prerequisites.sh' "$build_script" ||
    fail "build script does not run the prerequisite gate"

config=$root/toolchain/openssh/sshd_config
for directive in \
    'PasswordAuthentication no' \
    'PubkeyAuthentication yes' \
    'PermitRootLogin no' \
    'UsePAM no' \
    'UseDNS no' \
    'StrictModes yes' \
    'X11Forwarding no'; do
    grep -Eq "^[[:space:]]*$directive\$" "$config" ||
        fail "sshd_config missing safe default: $directive"
done
grep -Eq '^[[:space:]]*AddressFamily inet$' "$config" ||
    fail "sshd_config must restrict the test listener to IPv4"

if [ -d "$root/user/openssh/.git" ] || git -C "$root/user/openssh" rev-parse \
    --git-dir >/dev/null 2>&1; then
    actual=$(git -C "$root/user/openssh" rev-parse HEAD)
    [ "$actual" = "$openssh_commit" ] ||
        fail "OpenSSH source is not pinned to $openssh_commit"
fi
grep -q "$openssh_commit" "$root/toolchain/init-userspace.sh" ||
    fail "init-userspace.sh does not record the OpenSSH pin"

empty_sysroot=$tmp/empty-sysroot
mkdir -p "$empty_sysroot/include" "$empty_sysroot/lib"
set +e
ASTIA_SYSROOT="$empty_sysroot" \
    "$root/toolchain/openssh/check-prerequisites.sh" "$empty_sysroot" \
    >"$tmp/prerequisites.out" 2>&1
status=$?
set -e
[ "$status" -eq 3 ] ||
    fail "prerequisite gate did not fail with status 3 (got $status)"
for missing in netinet/in.h syslog.h chroot setresuid; do
    grep -q "$missing" "$tmp/prerequisites.out" ||
        fail "prerequisite output does not name $missing"
done

base_stage=$tmp/base
mkdir -p "$base_stage/etc" "$base_stage/bin" "$base_stage/sbin"
printf '%s\n' 'root::0:0:root:/root:/bin/zsh' \
    'user::1000:1000:User:/home/user:/bin/zsh' >"$base_stage/etc/passwd"
printf '%s\n' 'root::0:' 'users::1000:user' >"$base_stage/etc/group"
printf 'base-init\n' >"$base_stage/sbin/init"
chmod 0755 "$base_stage/sbin/init"
(cd "$base_stage" && find . -print | cpio -o --format=newc --quiet) \
    >"$tmp/base.cpio"

fake_build=$tmp/fake-build
mkdir -p "$fake_build/output" "$fake_build/etc" "$tmp/keys"
for binary in ssh sshd sshd-session ssh-keygen; do
    printf '#!/bin/sh\nexit 0\n' >"$fake_build/output/$binary"
    chmod 0755 "$fake_build/output/$binary"
done
cp "$config" "$fake_build/etc/sshd_config"
printf 'host-key\n' >"$tmp/keys/ssh_host_ed25519_key"
printf 'host-key.pub\n' >"$tmp/keys/ssh_host_ed25519_key.pub"
printf 'client-key\n' >"$tmp/keys/client_key"
printf 'client-key.pub\n' >"$tmp/keys/client_key.pub"
printf 'client-key.pub\n' >"$tmp/keys/authorized_keys"
printf 'test-init\n' >"$tmp/init"
chmod 0755 "$tmp/init"
printf '#!/bin/sh\n' >"$tmp/shell"
chmod 0755 "$tmp/shell"

OPENSSH_TEST_KEY_DIR="$tmp/keys" \
    OPENSSH_TEST_INIT="$tmp/init" \
    OPENSSH_SHELL_BINARY="$tmp/shell" \
    "$root/toolchain/package-openssh-initrd.sh" \
    "$tmp/base.cpio" "$fake_build" "$tmp/openssh.cpio" \
    >/dev/null

extract=$tmp/extract
mkdir -p "$extract"
(cd "$extract" && cpio -id --quiet --no-absolute-filenames) <"$tmp/openssh.cpio"

for path in \
    usr/bin/ssh usr/bin/ssh-keygen usr/sbin/sshd \
    usr/libexec/sshd-session etc/ssh/sshd_config \
    etc/ssh/ssh_host_ed25519_key etc/ssh/authorized_keys \
    etc/ssh/test_client_key var/empty run bin/zsh sbin/init; do
    test -e "$extract/$path" || fail "packaged image missing /$path"
done
[ "$(cat "$extract/sbin/init")" = test-init ] ||
    fail "packaged image did not install the test init"
[ "$(cat "$extract/etc/ssh/authorized_keys")" = client-key.pub ] ||
    fail "packaged image lost the test authorized key"
[ "$(stat -c %a "$extract/etc/ssh/ssh_host_ed25519_key")" = 600 ] ||
    fail "host key is not mode 0600"
[ "$(stat -c %a "$extract/var/empty")" = 755 ] ||
    fail "/var/empty is not mode 0755"
grep -q '^sshd:!:' "$extract/etc/passwd" ||
    fail "packaged passwd does not lock the sshd account"
grep -q '^sshd:' "$extract/etc/group" ||
    fail "packaged group does not contain the sshd group"

if git -C "$root" ls-files -- toolchain/openssh |
    grep -Eq 'ssh_host|client_key|id_ed25519'; then
    fail "tracked files contain test key material"
fi

echo "openssh port check passed"
