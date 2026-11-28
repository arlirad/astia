#!/bin/sh
set -eu

tool=${1:?tool path required}
work=${2:-${EXT4_ORPHAN_DIR:-${TMPDIR:-/tmp}/astia-ext4-orphan}}

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

printf 'orphan victim payload\n' >"$root/victim"

fixture=${work}/orphan-base.img

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 -O orphan_file \
    -d "$root" "$fixture" 16M
e2fsck -fn "$fixture"

inode=$(debugfs -R 'stat /victim' "$fixture" 2>/dev/null |
    sed -n 's/^Inode: \([0-9][0-9]*\).*/\1/p' | head -n 1)

if [ -z "$inode" ]; then
    echo "could not read /victim inode number from the fixture" >&2
    exit 1
fi

echo "fixture /victim inode: ${inode}"

launcher=${EXT4_ORPHAN_VFS_LAUNCHER:-}
if [ -z "$launcher" ] && command -v setarch >/dev/null 2>&1; then
    launcher='setarch -R'
fi

run_tool() {
    if [ -n "$launcher" ]; then
        $launcher "$tool" "$@"
    else
        "$tool" "$@"
    fi
}

free_inodes() {
    dumpe2fs -h "$1" 2>/dev/null |
        sed -n 's/^Free inodes: *//p' | head -n 1
}

features() {
    dumpe2fs -h "$1" 2>/dev/null |
        sed -n 's/^Filesystem features: *//p' | head -n 1
}

has_orphan_present() {
    printf '%s\n' "$(features "$1")" | grep -qw 'orphan_present'
}

check_result() {
    label=$1
    image=$2
    baseline=$3
    require_cleared=$4

    status=0
    e2fsck -fn "$image" || status=$?

    echo "${label}: e2fsck -fn exited ${status}"

    if [ "$status" -ne 0 ]; then
        echo "${label}: e2fsck rejected the orphan-cleaned image" >&2
        exit 1
    fi

    after=$(free_inodes "$image")

    echo "${label}: free inodes ${baseline} -> ${after}"

    if [ "$after" -ne $((baseline + 1)) ]; then
        echo "${label}: orphan cleanup did not free exactly one inode" >&2
        exit 1
    fi

    listing=$(debugfs -R 'ls -l /' "$image" 2>/dev/null)

    if printf '%s\n' "$listing" | grep -qw 'victim'; then
        echo "${label}: /victim still present in the cleaned image" >&2
        exit 1
    fi

    if [ "$require_cleared" = yes ] && has_orphan_present "$image"; then
        echo "${label}: orphan_present is still set after cleanup" >&2
        exit 1
    fi
}

fixture_with_unlinked_victim() {
    image=$1

    cp "$fixture" "$image"
    debugfs -w -R 'unlink /victim' "$image" >/dev/null 2>&1

    free_inodes "$image"
}

# Phase 1: the old-style s_last_orphan inode list.
list_image=${work}/orphan-list.img
list_baseline=$(fixture_with_unlinked_victim "$list_image")

echo "list phase: free inodes before cleanup: ${list_baseline}"
echo "list phase: orphan_present before cleanup: $(has_orphan_present "$list_image" && echo yes || echo no)"

run_tool "$list_image" "${work}/orphan-list-made.img" orphan-make-list "$inode" \
    >"${work}/list-make.log" 2>&1
cat "${work}/list-make.log"

if ! grep -qx 'OK' "${work}/list-make.log"; then
    echo "orphan-make-list did not report OK" >&2
    exit 1
fi

run_tool "${work}/orphan-list-made.img" "${work}/orphan-list-out.img" orphan-mount \
    >"${work}/list-mount.log" 2>&1
cat "${work}/list-mount.log"

if ! grep -qx 'OK' "${work}/list-mount.log"; then
    echo "orphan-mount did not report OK for the inode list phase" >&2
    exit 1
fi

check_result "list phase" "${work}/orphan-list-out.img" "$list_baseline" no

# Phase 2: the orphan file referenced by s_orphan_file_inum.
file_image=${work}/orphan-file.img
file_baseline=$(fixture_with_unlinked_victim "$file_image")

echo "file phase: free inodes before cleanup: ${file_baseline}"
echo "file phase: orphan_present before make: $(has_orphan_present "$file_image" && echo yes || echo no)"

run_tool "$file_image" "${work}/orphan-file-made.img" orphan-make-file "$inode" \
    >"${work}/file-make.log" 2>&1
cat "${work}/file-make.log"

if ! grep -qx 'OK' "${work}/file-make.log"; then
    echo "orphan-make-file did not report OK" >&2
    exit 1
fi

if ! has_orphan_present "${work}/orphan-file-made.img"; then
    echo "orphan-make-file did not set orphan_present" >&2
    exit 1
fi

echo "file phase: orphan_present after make: yes"

run_tool "${work}/orphan-file-made.img" "${work}/orphan-file-out.img" orphan-mount \
    >"${work}/file-mount.log" 2>&1
cat "${work}/file-mount.log"

if ! grep -qx 'OK' "${work}/file-mount.log"; then
    echo "orphan-mount did not report OK for the orphan file phase" >&2
    exit 1
fi

check_result "file phase" "${work}/orphan-file-out.img" "$file_baseline" yes

echo "file phase: orphan_present after cleanup: $(has_orphan_present "${work}/orphan-file-out.img" && echo yes || echo no)"

printf 'ext4 orphan cleanup test passed\n'
