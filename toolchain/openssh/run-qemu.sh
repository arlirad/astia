#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mode=${OPENSSH_QEMU_MODE:-sshd}
build=${OPENSSH_BUILD:-$root/build/openssh}
source=${OPENSSH_SOURCE:-$root/user/openssh}
sysroot=${ASTIA_SYSROOT:-$build/sysroot}
crt0=${ASTIA_CRT0:-$build/astia/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.cpio}
work=${OPENSSH_QEMU_RUN:-/tmp/astia-openssh-qemu}
qemu=${QEMU:-qemu-system-x86_64}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
timeout_seconds=${OPENSSH_QEMU_BOOT_TIMEOUT:-90}
key_dir=${OPENSSH_TEST_KEY_DIR:-$build/test}
client_key=$key_dir/client_key
shell_binary=${OPENSSH_SHELL_BINARY:-$root/build/user/zsh/output/zsh}
initrd=$work/openssh-initrd.cpio
log=$work/qemu.log

case "$mode" in
    sshd|loopback) ;;
    *)
        echo "openssh: OPENSSH_QEMU_MODE must be sshd or loopback" >&2
        exit 2
        ;;
esac

for command in "$qemu" ssh ssh-keygen timeout python3; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "openssh: required host command not found: $command" >&2
        exit 1
    }
done
set +e
OPENSSH_BUILD="$build" OPENSSH_SOURCE="$source" ASTIA_SYSROOT="$sysroot" \
    ASTIA_CRT0="$crt0" CLANG="$clang" "$root/toolchain/build-openssh.sh"
status=$?
set -e
if [ "$status" -eq 3 ]; then
    echo "openssh: prerequisites missing; skipping $mode QEMU test" >&2
    exit 77
fi
[ "$status" -eq 0 ] || exit "$status"

for input in "$kernel" "$base"; do
    test -f "$input" || {
        echo "openssh: QEMU test input missing: $input" >&2
        exit 1
    }
done

if [ ! -f "$shell_binary" ]; then
    echo "openssh: guest shell unavailable: $shell_binary" >&2
    echo "openssh: build zsh or set OPENSSH_SHELL_BINARY; skipping $mode QEMU test" >&2
    exit 77
fi

rm -rf "$work"
mkdir -p "$work"
resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtins=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtins" ]; then
    echo "openssh: clang compiler builtins not found" >&2
    exit 1
fi
common_flags="--target=$target -U__linux__ -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource_dir/include -isystem $sysroot/include"
# shellcheck disable=SC2086
"$clang" $common_flags -c "$root/toolchain/openssh/openssh-sshd-init.c" \
    -o "$work/openssh-sshd-init.o"
# shellcheck disable=SC2086
"$clang" $common_flags -nostdlib -nostartfiles -nodefaultlibs -static \
    -Wl,-no-pie -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$work/openssh-sshd-init" "$crt0" "$work/openssh-sshd-init.o" \
    "$sysroot/lib/libastia_libc.a" "$builtins"

OPENSSH_TEST_INIT="$work/openssh-sshd-init" \
    OPENSSH_SHELL_BINARY="$shell_binary" \
    OPENSSH_TEST_KEY_DIR="$key_dir" \
    OPENSSH_LIBC_SO="$sysroot/lib/libc.so" \
    "$root/toolchain/package-openssh-initrd.sh" "$base" "$build" "$initrd"

port=${OPENSSH_QEMU_PORT:-}
if [ -z "$port" ]; then
    port=$(python3 - <<'PY'
import socket

sock = socket.socket()
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
)
fi

"$qemu" -m 256M -kernel "$kernel" -initrd "$initrd" \
    -device rtl8139,netdev=net0 \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$port-:22" \
    -serial "file:$log" -display none -monitor none -no-reboot >/dev/null 2>&1 &
qemu_pid=$!
trap 'kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true' EXIT INT TERM

deadline=$(( $(date +%s) + timeout_seconds ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        cat "$log" >&2 || true
        echo "openssh: QEMU exited before sshd became ready" >&2
        exit 1
    fi
    if grep -q 'openssh-sshd-init: ready' "$log" 2>/dev/null &&
        ssh -p "$port" -i "$client_key" -o IdentitiesOnly=yes -o BatchMode=yes \
            -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
            -o ConnectTimeout=1 -o PreferredAuthentications=publickey \
            user@127.0.0.1 true >/dev/null 2>&1; then
        break
    fi
    sleep 1
done
if [ "$(date +%s)" -ge "$deadline" ]; then
    cat "$log" >&2 || true
    echo "openssh: timed out waiting for sshd on 127.0.0.1:$port" >&2
    exit 1
fi

ssh_host() {
    ssh -p "$port" -i "$client_key" -o IdentitiesOnly=yes -o BatchMode=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o ConnectTimeout=5 -o PreferredAuthentications=publickey "$@"
}

output=$(ssh_host user@127.0.0.1 "echo astia-ssh-ok") || {
    cat "$log" >&2 || true
    echo "openssh: host ssh command failed" >&2
    exit 1
}
[ "$output" = "astia-ssh-ok" ] || {
    echo "openssh: unexpected host ssh output: $output" >&2
    exit 1
}

wrong_key=$key_dir/wrong_key
if [ ! -f "$wrong_key" ]; then
    ssh-keygen -q -t ed25519 -N '' -f "$wrong_key"
fi
if ssh -p "$port" -i "$wrong_key" -o IdentitiesOnly=yes -o BatchMode=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o ConnectTimeout=5 -o PreferredAuthentications=publickey \
    user@127.0.0.1 true >/dev/null 2>&1; then
    echo "openssh: sshd accepted an unauthorized key" >&2
    exit 1
fi

if ssh -p "$port" -o BatchMode=yes -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password \
    -o NumberOfPasswordPrompts=0 user@127.0.0.1 true >/dev/null 2>&1; then
    echo "openssh: sshd accepted password authentication" >&2
    exit 1
fi

if ssh_host -l root 127.0.0.1 true >/dev/null 2>&1; then
    echo "openssh: sshd accepted root login" >&2
    exit 1
fi

if [ "$mode" = loopback ]; then
    output=$(ssh_host user@127.0.0.1 \
        '/usr/bin/ssh -p 22 -i /etc/ssh/test_client_key -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PreferredAuthentications=publickey user@127.0.0.1 echo astia-ssh-loopback-ok') || {
        cat "$log" >&2 || true
        echo "openssh: guest ssh loopback command failed" >&2
        exit 1
    }
    [ "$output" = "astia-ssh-loopback-ok" ] || {
        echo "openssh: unexpected guest ssh output: $output" >&2
        exit 1
    }
fi

echo "openssh: $mode QEMU test passed"
