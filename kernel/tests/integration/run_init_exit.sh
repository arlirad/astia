#!/bin/sh
# Boot the production kernel with a real initrd and check that init is launched
# once as PID 1, waited for until it exits, and that the exit is fatal.
set -eu

qemu=${QEMU:-qemu-system-x86_64}
kernel=${ASTIA_KERNEL:-}
initrd=${ASTIA_INITRD:-}
if [ -z "$kernel" ] || [ -z "$initrd" ]; then
    echo "ASTIA_KERNEL and ASTIA_INITRD must name the production kernel and initrd" >&2
    exit 2
fi
log=${TMPDIR:-/tmp}/astia-init-exit-$$.log
trap 'rm -f "$log"' EXIT INT TERM

set +e
timeout 20 "$qemu" -kernel "$kernel" -initrd "$initrd" \
    -serial "file:$log" -display none -monitor none -no-reboot
status=$?
set -e

cat "$log"
if [ "$status" -ne 124 ]; then
    echo "expected kernel panic to hold the machine, QEMU status was $status" >&2
    exit 1
fi

line_of() {
    grep -nF "$1" "$log" | head -n 1 | cut -d: -f1 || true
}

launched=$(line_of "init launched pid=1")
exited=$(line_of "init exited pid=1 status=0")
terminated=$(line_of "init terminated")
launch_count=$(grep -cF "init launched pid=" "$log" || true)

fail() {
    echo "production init lifecycle: $1" >&2
    exit 1
}

[ -n "$launched" ] || fail "init was not launched as pid 1"
[ "$launch_count" -eq 1 ] || fail "init was launched $launch_count times"
[ -n "$exited" ] || fail "init did not exit with status 0"
[ -n "$terminated" ] || fail "init exit was not fatal"
grep -qF "PANIC" "$log" || fail "init exit did not panic"
if grep -qF "init launch failed" "$log"; then
    fail "init launch reported a failure"
fi
if [ "$launched" -ge "$exited" ] || [ "$exited" -ge "$terminated" ]; then
    fail "kernel did not wait for init before reporting its exit as fatal"
fi
