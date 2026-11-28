#!/bin/sh
# Boot an ACPI q35 GRUB image, or the synthetic malformed-fixture kernel
# through QEMU's direct boot path.  The syscall modes boot images whose
# /sbin/init issues the userspace power syscall.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/qemu_timeout.sh"

usage='usage: run_acpi_q35.sh shutdown|reject-rsdp|reject-aml|syscall-reboot|syscall-shutdown QEMU IMAGE'
mode=${1:?$usage}
qemu=${2:?$usage}
image=${3:?$usage}
qemu_timeout_seconds=$(boot_timeout_seconds)
log=${TMPDIR:-/tmp}/astia-acpi-$mode-$$.log
trap 'rm -f "$log"' EXIT INT TERM

set +e
case "$mode" in
    shutdown)
        timeout "$qemu_timeout_seconds" "$qemu" -machine q35 -m 128M -cdrom "$image" \
            -serial "file:$log" -display none -monitor none -no-reboot
        status=$?
        ;;
    reject-rsdp | reject-aml)
        timeout 20 "$qemu" -machine q35 -m 128M -kernel "$image" \
            -append "--acpi-test=$(printf '%s' "$mode" | sed 's/^reject-/bad-/')" \
            -serial "file:$log" -display none -monitor none \
            -device isa-debug-exit -no-reboot
        status=$?
        ;;
    syscall-reboot | syscall-shutdown)
        timeout "$qemu_timeout_seconds" "$qemu" -machine q35 -m 128M -cdrom "$image" \
            -serial "file:$log" -display none -monitor none \
            -device isa-debug-exit -no-reboot
        status=$?
        ;;
    *)
        echo "run_acpi_q35: unknown mode $mode" >&2
        exit 2
        ;;
esac
set -e

cat "$log"

if grep -qF "PANIC" "$log"; then
    echo "run_acpi_q35: unexpected panic" >&2
    exit 1
fi

case "$mode" in
    shutdown)
        test "$status" -eq 0
        grep -qF "acpi: S5 ready" "$log"
        grep -qF "acpi: PRT probe irq=" "$log"
        grep -qF "shutdown: tier=final handler=acpi-s5" "$log"
        grep -qF "core: runtime ok" "$log"
        ;;
    reject-rsdp)
        test "$status" -eq 1
        grep -qF "acpi: rejected (RSDP checksum)" "$log"
        grep -qF "core: runtime ok" "$log"
        ;;
    reject-aml)
        test "$status" -eq 1
        grep -qF "acpi: AML rejected" "$log"
        grep -qF "core: runtime ok" "$log"
        ;;
    syscall-reboot)
        test "$status" -eq 0
        grep -qF "acpi: reset ready value=" "$log"
        grep -qF "shutdown: tier=final handler=override" "$log"
        ! grep -qF "shutdown: tier=final handler=acpi-s5" "$log"
        ! grep -qF "acpi: reset write returned without resetting" "$log"
        ! grep -qF "core: selftest ok" "$log"
        ;;
    syscall-shutdown)
        test "$status" -eq 0
        grep -qF "acpi: S5 ready" "$log"
        grep -qF "shutdown: tier=final handler=acpi-s5" "$log"
        ! grep -qF "shutdown: tier=final handler=override" "$log"
        ! grep -qF "core: selftest ok" "$log"
        ;;
esac
