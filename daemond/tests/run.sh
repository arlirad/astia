#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${DAEMOND_TEST_BUILD:-${TMPDIR:-/tmp}/astia-daemond-test-build}
cc=${CC:-cc}
cflags="-std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror"

rm -rf "$out"
mkdir -p "$out"

# shellcheck disable=SC2086
$cc $cflags -I "$root/daemond" \
    "$root/daemond/tests/stop_test.c" "$root/daemond/stop.c" \
    -o "$out/stop_test"
"$out/stop_test"

socket_path="${TMPDIR:-/tmp}/astia-daemond-$$.sock"
fallback_path="${TMPDIR:-/tmp}/astia-daemond-$$-fallback.sock"
trap 'rm -f "$socket_path" "$fallback_path"' EXIT INT TERM

# The supervisor runs unchanged on the host; only spawnve() and the socket
# paths are supplied by this harness.
# shellcheck disable=SC2086
$cc $cflags -I "$root/daemond" -idirafter "$root/libc/include" \
    -include "$root/daemond/tests/host_spawn.h" \
    -Dmain=astia_daemond_unused_main \
    -Dsetpgid=host_setpgid \
    -DDAEMON_SOCKET_PATH="\"$socket_path\"" \
    -DDAEMON_SOCKET_FALLBACK="\"$fallback_path\"" \
    -c "$root/daemond/daemond.c" -o "$out/daemond.o"
# shellcheck disable=SC2086
$cc $cflags -I "$root/daemond" -idirafter "$root/libc/include" \
    -DDAEMON_SOCKET_PATH="\"$socket_path\"" \
    -DDAEMON_SOCKET_FALLBACK="\"$fallback_path\"" \
    "$root/daemond/tests/daemond_stop_test.c" \
    "$root/daemond/tests/host_spawn.c" "$root/daemond/stop.c" \
    "$out/daemond.o" -o "$out/daemond_stop_test"
"$out/daemond_stop_test"

echo "daemond tests ok"
