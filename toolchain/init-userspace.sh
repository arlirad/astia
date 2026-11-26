#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
nano_path=$root/user/nano
nano_version=8.6
nano_url=https://ftp.gnu.org/gnu/nano/nano-8.6.tar.xz
nano_sha256=f7abfbf0eed5f573ab51bd77a458f32d82f9859c55e9689f819d96fe1437a619
openssh_version=V_9_9_P2
openssh_commit=d76b2675116617394cd355a3437b4963a562b64d
cache=${ASTIA_USERSPACE_CACHE:-$root/build/userspace/cache}
archive=$cache/nano-$nano_version.tar.xz

git -C "$root" submodule update --init --recursive user/zsh
git -C "$root" submodule update --init --recursive user/gnulib
git -C "$root" submodule update --init --recursive user/ncurses
git -C "$root" submodule update --init --recursive user/openssh
openssh_actual=$(git -C "$root/user/openssh" rev-parse HEAD)
if [ "$openssh_actual" != "$openssh_commit" ]; then
    echo "openssh: expected $openssh_version ($openssh_commit), found $openssh_actual" >&2
    exit 1
fi

if timeout 45 git -C "$root" submodule update --init user/nano; then
    exit 0
fi

echo "nano submodule unavailable; using the pinned GNU archive fallback" >&2
git -C "$root" submodule deinit -f -- user/nano >/dev/null 2>&1 || true
mkdir -p "$cache"
if [ ! -f "$archive" ]; then
    temporary=$archive.$$
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 2 "$nano_url" -o "$temporary"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$nano_url" -O "$temporary"
    else
        echo "nano: curl or wget is required to fetch $nano_url" >&2
        exit 1
    fi
    mv "$temporary" "$archive"
    trap - EXIT HUP INT TERM
fi

actual=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual" != "$nano_sha256" ]; then
    echo "nano: checksum mismatch for $archive" >&2
    exit 1
fi

staging=$cache/nano-$nano_version-source
rm -rf "$staging" "$nano_path"
mkdir -p "$staging"
tar -xf "$archive" -C "$staging" --strip-components=1
mkdir -p "$(dirname -- "$nano_path")"
mv "$staging" "$nano_path"
test -x "$nano_path/configure"
