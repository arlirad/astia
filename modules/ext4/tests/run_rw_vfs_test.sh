#!/bin/sh
set -eu

launcher=${1:-}
tool=${2:?tool path required}

work=${EXT4_RW_DIR:-${TMPDIR:-/tmp}/astia-ext4-rw}

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/etc" "$root/dir"
printf 'read-only ext4 fixture\n' >"$root/etc/known.txt"
printf 'nested\n' >"$root/dir/nested.txt"

run_pass() {
    label=$1
    parking=$2

    fixture=${work}/fixture-${label}.img
    out=${work}/result-${label}.img

    E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -I 256 \
        -O '^has_journal,^64bit,^flex_bg,^orphan_file,^metadata_csum_seed' \
        -d "$root" "$fixture" 16M
    e2fsck -fn "$fixture"

    if [ "$parking" = "1" ]; then
        run_env="EXT4_RW_PARKING=1"
    else
        run_env=""
    fi

    if [ -n "$launcher" ]; then
        env $run_env "$launcher" -R "$tool" "$fixture" "$out" \
            >"${work}/tool-${label}.log" 2>&1
    else
        env $run_env "$tool" "$fixture" "$out" \
            >"${work}/tool-${label}.log" 2>&1
    fi

    cat "${work}/tool-${label}.log"

    if ! grep -qx 'OK' "${work}/tool-${label}.log"; then
        echo "ext4 read-write VFS tool did not report OK (${label})" >&2
        exit 1
    fi

    if [ "$parking" = "1" ] &&
        ! grep -Eq '^PARKED [1-9][0-9]*$' "${work}/tool-${label}.log"; then
        echo "parking device did not report any parked requests (${label})" >&2
        exit 1
    fi

    e2fsck -fn "$out"

    content=$(debugfs -R 'cat /result/persisted.txt' "$out" 2>/dev/null)
    printf '%s\n' "$content"

    if ! printf '%s\n' "$content" | grep -q 'persisted through ext4 module'; then
        echo "persisted.txt content missing (${label})" >&2
        exit 1
    fi

    listing=$(debugfs -R 'ls -l /' "$out" 2>/dev/null)
    printf '%s\n' "$listing"

    for name in etc dir result; do
        if ! printf '%s\n' "$listing" | grep -Eq "(^|[[:space:]])${name}$"; then
            echo "expected directory ${name} missing from / (${label})" >&2
            exit 1
        fi
    done

    for name in hello sub; do
        if printf '%s\n' "$listing" | grep -Eq "(^|[[:space:]])${name}$"; then
            echo "removed node ${name} survived the delete phase (${label})" >&2
            exit 1
        fi
    done

    before=$(debugfs -R 'stat /etc/known.txt' "$fixture" 2>/dev/null)
    after=$(debugfs -R 'stat /etc/known.txt' "$out" 2>/dev/null)

    if [ "$before" != "$after" ]; then
        echo "/etc/known.txt changed (${label})" >&2
        printf '%s\n' "$before" >&2
        printf '%s\n' "$after" >&2
        exit 1
    fi
}

run_pass sync 0
run_pass parking 1

printf 'ext4 read-write VFS test passed\n'
