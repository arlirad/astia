#!/bin/sh
set -eu

tool=${1:?tool path required}
work=${2:-${EXT4_ORPHAN_DIR:-${TMPDIR:-/tmp}/astia-ext4-orphan}}

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

printf 'orphan victim payload\n' >"$root/victim"
printf 'second orphan payload\n' >"$root/second"

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

inode2=$(debugfs -R 'stat /second' "$fixture" 2>/dev/null |
    sed -n 's/^Inode: \([0-9][0-9]*\).*/\1/p' | head -n 1)

if [ -z "$inode2" ]; then
    echo "could not read /second inode number from the fixture" >&2
    exit 1
fi

echo "fixture /victim inode: ${inode}"
echo "fixture /second inode: ${inode2}"

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

# Phase 3: the checksum on the orphan file block must be verified before any
# inode is freed. Corrupting it must make a writable mount fail without
# touching the image.
corrupt_image=${work}/orphan-file-corrupt.img
corrupt_before=${work}/orphan-file-corrupt-before.img

run_tool "${work}/orphan-file-made.img" "$corrupt_image" orphan-corrupt-file \
    >"${work}/corrupt-make.log" 2>&1
cat "${work}/corrupt-make.log"

if ! grep -qx 'OK' "${work}/corrupt-make.log"; then
    echo "orphan-corrupt-file did not report OK" >&2
    exit 1
fi

cp "$corrupt_image" "$corrupt_before"

if run_tool "$corrupt_image" "${work}/orphan-file-corrupt-out.img" orphan-mount \
    >"${work}/corrupt-mount.log" 2>&1; then
    echo "orphan-mount accepted a corrupted orphan file checksum" >&2
    cat "${work}/corrupt-mount.log" >&2
    exit 1
fi

cat "${work}/corrupt-mount.log"

if [ -f "${work}/orphan-file-corrupt-out.img" ]; then
    echo "corrupted orphan-file mount still wrote an output image" >&2
    exit 1
fi

if ! cmp -s "$corrupt_image" "$corrupt_before"; then
    echo "corrupted orphan-file image changed during the refused mount" >&2
    exit 1
fi

echo "corrupt phase: mount refused and image unchanged"

# Phase 4: a two-inode s_last_orphan chain must free both inodes.
chain_image=${work}/orphan-chain.img

cp "$fixture" "$chain_image"
debugfs -w -R 'unlink /victim' "$chain_image" >/dev/null 2>&1
debugfs -w -R 'unlink /second' "$chain_image" >/dev/null 2>&1

chain_baseline=$(free_inodes "$chain_image")

echo "chain phase: free inodes before cleanup: ${chain_baseline}"

run_tool "$chain_image" "${work}/orphan-chain-made.img" orphan-make-chain \
    "$inode" "$inode2" >"${work}/chain-make.log" 2>&1
cat "${work}/chain-make.log"

if ! grep -qx 'OK' "${work}/chain-make.log"; then
    echo "orphan-make-chain did not report OK" >&2
    exit 1
fi

run_tool "${work}/orphan-chain-made.img" "${work}/orphan-chain-out.img" \
    orphan-mount >"${work}/chain-mount.log" 2>&1
cat "${work}/chain-mount.log"

if ! grep -qx 'OK' "${work}/chain-mount.log"; then
    echo "orphan-mount did not report OK for the inode chain phase" >&2
    exit 1
fi

status=0
e2fsck -fn "${work}/orphan-chain-out.img" >"${work}/chain-fsck.log" 2>&1 || status=$?
cat "${work}/chain-fsck.log"

echo "chain phase: e2fsck -fn exited ${status}"

if [ "$status" -ne 0 ]; then
    echo "chain phase: e2fsck rejected the orphan-cleaned image" >&2
    exit 1
fi

chain_after=$(free_inodes "${work}/orphan-chain-out.img")

echo "chain phase: free inodes ${chain_baseline} -> ${chain_after}"

if [ "$chain_after" -ne $((chain_baseline + 2)) ]; then
    echo "chain phase: cleanup did not free exactly two inodes" >&2
    exit 1
fi

chain_listing=$(debugfs -R 'ls -l /' "${work}/orphan-chain-out.img" 2>/dev/null)

if printf '%s\n' "$chain_listing" | grep -qw 'victim' ||
    printf '%s\n' "$chain_listing" | grep -qw 'second'; then
    echo "chain phase: an orphan inode still has a directory entry" >&2
    exit 1
fi

printf 'ext4 orphan cleanup test passed\n'
