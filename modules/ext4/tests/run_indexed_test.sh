#!/bin/sh
set -eu

tool=${1:?tool path required}
work=${2:-${EXT4_INDEXED_DIR:-${TMPDIR:-/tmp}/astia-ext4-indexed}}

rm -rf "${work}"
mkdir -p "${work}"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/big"

awk 'BEGIN {
    for (i = 0; i < 120; i++) {
        n = 100 + (i % 101)
        s = sprintf("file-%03d-", i)

        while (length(s) < n)
            s = s "abcdefghijklmnopqrstuvwxyz"

        print substr(s, 1, n)
    }
}' >"${work}/names.txt"

while IFS= read -r name; do
    printf 'content %s\n' "$name" >"$root/big/$name"
done <"${work}/names.txt"

printf 'indexed fixture known payload\n' >"$root/big/known.txt"

image=${work}/indexed.img
output=${work}/indexed-out.img

E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -F -t ext4 -b 1024 \
    -d "$root" "$image" 16M
e2fsck -fn "$image"

status=0
e2fsck -fD -y "$image" >"${work}/index.log" 2>&1 || status=$?
cat "${work}/index.log"

if [ "$status" -gt 1 ]; then
    echo "e2fsck -fD exited ${status} while indexing the fixture" >&2
    exit 1
fi

flags=$(debugfs -R 'stat /big' "$image" 2>/dev/null |
    sed -n 's/.*Flags: *\(0x[0-9a-fA-F]*\).*/\1/p' | head -n 1)

if [ -z "$flags" ]; then
    echo "could not read the /big inode flags from the fixture" >&2
    exit 1
fi

echo "fixture /big Flags: $flags"

if [ $((flags & 0x81000)) -ne $((0x81000)) ]; then
    echo "fixture /big is not HTree-indexed (Flags: $flags)" >&2
    exit 1
fi

debugfs -R 'htree /big' "$image" >"${work}/htree.log" 2>&1 || {
    echo "debugfs htree /big failed on the fixture" >&2
    cat "${work}/htree.log" >&2
    exit 1
}

cat "${work}/htree.log"

if ! grep -q 'Root node dump' "${work}/htree.log" ||
    grep -q 'Not a hash-indexed directory' "${work}/htree.log"; then
    echo "fixture /big did not become a hash-indexed directory" >&2
    exit 1
fi

launcher=${EXT4_INDEXED_VFS_LAUNCHER:-}
if [ -z "$launcher" ] && command -v setarch >/dev/null 2>&1; then
    launcher='setarch -R'
fi

if [ -n "$launcher" ]; then
    $launcher "$tool" "$image" "$output" indexed >"${work}/tool.log" 2>&1
else
    "$tool" "$image" "$output" indexed >"${work}/tool.log" 2>&1
fi

cat "${work}/tool.log"

if ! grep -qx 'OK' "${work}/tool.log"; then
    echo "ext4 indexed-directory VFS tool did not report OK" >&2
    exit 1
fi

status=0
e2fsck -fn "$output" >"${work}/fsck.log" 2>&1 || status=$?
cat "${work}/fsck.log"

echo "e2fsck -fn ${output} exited ${status}"

if [ "$status" -ne 0 ]; then
    echo "e2fsck -fn rejected the converted directory" >&2
    exit 1
fi

if ! grep -q 'Pass 5: Checking group summary information' "${work}/fsck.log"; then
    echo "e2fsck did not complete directory checking (no pass 5)" >&2
    exit 1
fi

if ! debugfs -R 'stat /other/big' "$output" 2>/dev/null | grep -q '^Inode:'; then
    echo "renamed directory /other/big is missing from the output" >&2
    exit 1
fi

if debugfs -R 'stat /big' "$output" 2>/dev/null | grep -q '^Inode:'; then
    echo "old path /big still exists in the output" >&2
    exit 1
fi

listing=$(debugfs -R 'ls -l /other/big' "$output" 2>/dev/null)
printf '%s\n' "$listing"

for name in added.txt added2.txt known.txt; do
    if ! printf '%s\n' "$listing" | grep -Fq "$name"; then
        echo "expected entry ${name} missing from renamed /other/big" >&2
        exit 1
    fi
done

missing=0
while IFS= read -r name; do
    printf '%s\n' "$listing" | grep -Fq "$name" || missing=$((missing + 1))
done <"${work}/names.txt"

if [ "$missing" -ne 0 ]; then
    echo "${missing} original entries missing from renamed /other/big" >&2
    exit 1
fi

added=$(debugfs -R 'cat /other/big/added.txt' "$output" 2>/dev/null || true)
printf '%s\n' "$added"

if [ "$added" != 'added through one-time deindex' ]; then
    echo "/other/big/added.txt content mismatch after conversion" >&2
    exit 1
fi

second=$(debugfs -R 'cat /other/big/added2.txt' "$output" 2>/dev/null || true)
printf '%s\n' "$second"

if [ "$second" != 'second insertion on linear directory' ]; then
    echo "/other/big/added2.txt content mismatch after conversion" >&2
    exit 1
fi

known=$(debugfs -R 'cat /other/big/known.txt' "$output" 2>/dev/null || true)

if [ "$known" != 'indexed fixture known payload' ]; then
    echo "/other/big/known.txt content mismatch after conversion" >&2
    exit 1
fi

after_flags=$(debugfs -R 'stat /other/big' "$output" 2>/dev/null |
    sed -n 's/.*Flags: *\(0x[0-9a-fA-F]*\).*/\1/p' | head -n 1)

if [ -z "$after_flags" ]; then
    echo "could not read the /other/big inode flags from the output" >&2
    exit 1
fi

echo "converted /other/big Flags: $after_flags"

if [ $((after_flags & 0x1000)) -ne 0 ]; then
    echo "converted /other/big still carries the index flag (Flags: $after_flags)" >&2
    exit 1
fi

if [ $((after_flags & 0x80000)) -eq 0 ]; then
    echo "converted /other/big lost the extents flag (Flags: $after_flags)" >&2
    exit 1
fi

printf 'ext4 indexed directory conversion test passed\n'
