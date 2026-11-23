#!/usr/bin/env bash
set -Eeuo pipefail

die() {
    printf 'aarch64 virt kernel smoke: error: %s\n' "$*" >&2
    exit 1
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ASTIA_AARCH64_KERNEL_BUILD:-${root}/build/aarch64-virt}"
qemu="${QEMU:-qemu-system-aarch64}"
readelf="${READELF:-readelf}"
cmake_bin="${CMAKE:-cmake}"
toolchain="${root}/cmake/toolchain-aarch64-virt.cmake"
timeout_seconds="${ASTIA_AARCH64_VIRT_TIMEOUT:-20}"

for tool in "$cmake_bin" "$readelf" "$qemu"; do
    command -v "$tool" >/dev/null 2>&1 ||
        die "required tool '$tool' is not available"
done

[[ -f "$toolchain" ]] || die "missing toolchain file: $toolchain"

"$cmake_bin" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DARCH=aarch64 \
    -DPLATFORM=virt \
    -DBOOT=virt \
    -DASTIA_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Debug
"$cmake_bin" --build "$build" --target astiakrn --parallel

image="${build}/src/astiakrn"
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

grep -Fxq 'ASTIA AARCH64 VIRT BOOT: EARLY STAGE' "$log" ||
    die "early boot banner was not printed"
grep -Fq 'entry-x0: 0x0000000000000000' "$log" ||
    die "unexpected entry x0 value"
grep -Fq 'current-el: 0x1' "$log" ||
    die "production stage did not validate EL1"
grep -Eq '^dtb: address=0x40000000 size=0x[0-9a-f]{8}$' "$log" ||
    die "DTB address/totalsize line is missing or malformed"
dtb_size_hex="$(sed -nE \
    's/^dtb: address=0x40000000 size=0x([0-9a-f]{8})$/\1/p' \
    "$log" | head -n 1)"
[[ "$dtb_size_hex" =~ ^[0-9a-f]{8}$ ]] ||
    die "cannot parse DTB totalsize from serial output"
dtb_size=$((0x${dtb_size_hex}))
(( dtb_size >= 0x100 && dtb_size <= 0x100000 )) ||
    die "DTB totalsize 0x${dtb_size_hex} is outside the 1 MiB window"
grep -Fxq 'memory: base=0x0000000040000000 size=0x0000000010000000' "$log" ||
    die "RAM range was not reported as the expected 256 MiB at 0x40000000"
grep -Fxq 'console: base=0x0000000009000000 size=0x00001000 source=device-tree' \
    "$log" ||
    die "PL011 console was not resolved through /chosen/stdout-path"
grep -Fxq 'console-path: /pl011@9000000' "$log" ||
    die "resolved console path is missing"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: HANDOFF OK' "$log" ||
    die "production boot handoff was not reported"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: MMU ON' "$log" ||
    die "post-MMU marker was not reported"
grep -Fxq 'mmu: mair=0x00000000aa44ff04 tcr=0x0000000200903510' "$log" ||
    die "MAIR_EL1/TCR_EL1 policy line is missing or unexpected"
grep -Eq '^mmu: sctlr=0x[0-9a-f]{16} m=1 c=1 i=1$' "$log" ||
    die "SCTLR_EL1 M/C/I state is missing"
grep -Fxq 'mmu: pl011 write=ok' "$log" ||
    die "post-MMU PL011 write marker is missing"
grep -Eq '^mmu: pl011 read=0x[0-9a-f]{8}$' "$log" ||
    die "post-MMU PL011 read marker is missing"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: PL011 RW OK' "$log" ||
    die "post-MMU PL011 read/write verification was not reported"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: HALT' "$log" ||
    die "production stage did not halt cleanly"

printf '%s\n' 'aarch64 virt kernel smoke: PASS'
