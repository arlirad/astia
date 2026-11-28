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

fetch_pinned_source() {
    name=$1
    version=$2
    url=$3
    expected=$4
    destination=$5
    marker=$destination/.astia-source-version

    if [ -f "$marker" ] && [ "$(cat "$marker")" = "$name-$version" ] &&
        [ -x "$destination/configure" ]; then
        return 0
    fi

    mkdir -p "$cache"
    source_archive=$cache/$name-$version.tar.gz
    if [ ! -f "$source_archive" ]; then
        source_temporary=$source_archive.$$
        trap 'rm -f "$source_temporary"' EXIT HUP INT TERM
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL --retry 2 "$url" -o "$source_temporary"
        elif command -v wget >/dev/null 2>&1; then
            wget -q "$url" -O "$source_temporary"
        else
            echo "$name: curl or wget is required to fetch $url" >&2
            exit 1
        fi
        mv "$source_temporary" "$source_archive"
        trap - EXIT HUP INT TERM
    fi

    actual=$(sha256sum "$source_archive" | awk '{print $1}')
    if [ "$actual" != "$expected" ]; then
        echo "$name: checksum mismatch for $source_archive" >&2
        exit 1
    fi

    source_staging=$cache/$name-$version-source.$$
    rm -rf "$source_staging" "$destination"
    mkdir -p "$source_staging"
    tar -xf "$source_archive" -C "$source_staging" --strip-components=1
    printf '%s\n' "$name-$version" >"$source_staging/.astia-source-version"
    mkdir -p "$(dirname -- "$destination")"
    mv "$source_staging" "$destination"
}

fetch_pinned_source curl 8.23.0 \
    https://curl.se/download/curl-8.23.0.tar.gz \
    e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49 \
    "$root/user/curl"
fetch_pinned_source wget 1.25.0 \
    https://ftp.gnu.org/gnu/wget/wget-1.25.0.tar.gz \
    766e48423e79359ea31e41db9e5c289675947a7fcf2efdcedb726ac9d0da3784 \
    "$root/user/wget"

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
