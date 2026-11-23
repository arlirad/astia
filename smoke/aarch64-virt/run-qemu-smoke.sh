#!/usr/bin/env bash
set -Eeuo pipefail

die() {
    printf 'aarch64 virt smoke: error: %s\n' "$*" >&2
    exit 1
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build="${ASTIA_AARCH64_VIRT_BUILD:-${root}/build}"
qemu="${QEMU:-qemu-system-aarch64}"
readelf="${READELF:-readelf}"
toolchain="${root}/cmake/aarch64-virt-elf.cmake"
linker="${root}/linker/aarch64-virt.ld"
timeout_seconds="${ASTIA_AARCH64_VIRT_TIMEOUT:-20}"

for tool in cmake "$readelf" "$qemu"; do
    command -v "$tool" >/dev/null 2>&1 ||
        die "required tool '$tool' is not available"
done

[[ -f "$toolchain" ]] || die "missing toolchain file: $toolchain"
[[ -f "$linker" ]] || die "missing linker script: $linker"

cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build"

image="${build}/astia-aarch64-virt-smoke.elf"
[[ -f "$image" ]] || die "build did not produce $image"

header="$("$readelf" -h "$image")"
grep -q 'Class:.*ELF64' <<<"$header" ||
    die "image is not ELF64"
grep -q 'Machine:.*AArch64' <<<"$header" ||
    die "image is not AArch64"
grep -q 'Type:.*EXEC' <<<"$header" ||
    die "image is not a static executable"
entry_address="$(awk '/Entry point address:/ { print $4 }' <<<"$header")"
[[ "$entry_address" =~ ^0x[0-9a-fA-F]+$ ]] ||
    die "cannot parse ELF entry point '$entry_address'"
(( entry_address == 0x40400000 )) ||
    die "unexpected ELF entry point $entry_address"

load_addresses="$("$readelf" -lW "$image" | awk '$1 == "LOAD" { print $4 }')"
[[ -n "$load_addresses" ]] || die "image has no LOAD segments"

first_load="$(head -n 1 <<<"$load_addresses")"
[[ "$first_load" =~ ^0x[0-9a-fA-F]+$ ]] ||
    die "cannot parse first LOAD address '$first_load'"
(( first_load == 0x40400000 )) ||
    die "first LOAD segment is $first_load, not 0x40400000"

while IFS= read -r address; do
    [[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] ||
        die "cannot parse LOAD address '$address'"
    (( address >= 0x40100000 )) ||
        die "LOAD segment $address overlaps the 1 MiB QEMU DTB area"
done <<<"$load_addresses"

log="${build}/qemu-smoke.log"
set +e
timeout "$timeout_seconds" "$qemu" \
    -machine virt,secure=off,virtualization=off \
    -cpu cortex-a57 \
    -m 256M \
    -smp 1 \
    -display none \
    -monitor none \
    -serial stdio \
    -no-reboot \
    -kernel "$image" >"$log" 2>&1
qemu_status=$?
set -e

printf '%s\n' '--- QEMU serial output ---'
cat "$log"
printf '%s\n' '--- end QEMU serial output ---'

[[ "$qemu_status" -eq 0 ]] ||
    die "QEMU exited with status $qemu_status"
grep -Fxq 'ASTIA AARCH64 VIRT SMOKE: BOOT OK' "$log" ||
    die "boot banner was not printed"
grep -Fq 'entry-x0: 0x0000000000000000' "$log" ||
    die "unexpected entry x0 value"
grep -Fq 'entry-sp: 0x0000000000000000' "$log" ||
    die "unexpected entry sp value"
grep -Fq 'current-el: 0x1' "$log" ||
    die "guest did not enter at EL1"
grep -Fq 'entry-daif: 0x3c0' "$log" ||
    die "interrupt masks were not observed at entry"
grep -Eq '^dtb: address=0x40000000 magic=0xd00dfeed size=0x[0-9a-f]{8} valid=yes$' "$log" ||
    die "DTB handoff, magic, or totalsize validation failed"
dtb_size_hex="$(sed -nE \
    's/^dtb: address=0x40000000 magic=0xd00dfeed size=0x([0-9a-f]{8}) valid=yes$/\1/p' \
    "$log" | head -n 1)"
[[ "$dtb_size_hex" =~ ^[0-9a-f]{8}$ ]] ||
    die "cannot parse DTB totalsize from serial output"
dtb_size=$((0x${dtb_size_hex}))
(( dtb_size >= 0x100 && dtb_size <= 0x100000 )) ||
    die "DTB totalsize 0x${dtb_size_hex} is outside the 1 MiB window"
grep -Fxq 'ASTIA AARCH64 VIRT SMOKE: PASS' "$log" ||
    die "smoke did not report PASS"

printf '%s\n' 'aarch64 virt smoke: PASS'
