#!/bin/sh
# Shared bounded deadline for the QEMU boot harnesses.  Sourced, not executed.

# The default sits roughly 2x above a normal boot so that only a genuine hang
# trips the watchdog.  CTest grants the matching tests 300s (360s for the
# two-boot urandom test), so the cap keeps the internal deadline below the
# outer budget and a stalled guest still reports its serial log.
#
# Only one-to-three digit overrides are accepted.  `timeout` takes 64-bit
# values, and shell arithmetic is not a portable way to detect that an
# override overflowed the machine word, so the width is rejected before any
# comparison.
boot_timeout_seconds() {
    seconds=${ASTIA_BOOT_QEMU_TIMEOUT:-240}
    case "$seconds" in
        '' | *[!0-9]*) seconds=240 ;;
    esac
    if [ "${#seconds}" -gt 3 ]; then
        seconds=240
    elif [ "$seconds" -lt 1 ]; then
        seconds=1
    elif [ "$seconds" -gt 240 ]; then
        seconds=240
    fi
    printf '%s\n' "$seconds"
}
