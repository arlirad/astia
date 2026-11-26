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

symbol_address() {
    local symbol="$1"
    local value
    value="$("$readelf" -sW "$image" |
        awk -v name="$symbol" '$8 == name { print $2; exit }')"
    [[ "$value" =~ ^[0-9a-fA-F]+$ ]] ||
        die "symbol '$symbol' is missing from $image"
    printf '%s\n' "$value"
}

vector_table="$(symbol_address __vector_table)"
vector_table_end="$(symbol_address __vector_table_end)"
brk_probe="$(symbol_address astia_aarch64_brk_probe)"
vector_table_value=$((0x${vector_table}))
vector_table_end_value=$((0x${vector_table_end}))
brk_probe_value=$((0x${brk_probe}))

(( (vector_table_value & 0x7ff) == 0 )) ||
    die "vector table 0x$(printf '%x' "$vector_table_value") is not 2 KiB aligned"
(( vector_table_end_value - vector_table_value == 0x800 )) ||
    die "vector table size is not 0x800 bytes"

check_vector_slot() {
    local name="$1"
    local offset="$2"
    local address
    address="$(symbol_address "$name")"
    (( 0x${address} == vector_table_value + offset )) ||
        die "$name is at 0x${address}, expected vector base + $offset"
}

check_vector_slot __vector_current_el_sp0_sync 0x000
check_vector_slot __vector_current_el_sp0_irq 0x080
check_vector_slot __vector_current_el_sp0_fiq 0x100
check_vector_slot __vector_current_el_sp0_serror 0x180
check_vector_slot __vector_current_el_spx_sync 0x200
check_vector_slot __vector_current_el_spx_irq 0x280
check_vector_slot __vector_current_el_spx_fiq 0x300
check_vector_slot __vector_current_el_spx_serror 0x380
check_vector_slot __vector_lower_el_aarch64_sync 0x400
check_vector_slot __vector_lower_el_aarch64_irq 0x480
check_vector_slot __vector_lower_el_aarch64_fiq 0x500
check_vector_slot __vector_lower_el_aarch64_serror 0x580
check_vector_slot __vector_lower_el_aarch32_sync 0x600
check_vector_slot __vector_lower_el_aarch32_irq 0x680
check_vector_slot __vector_lower_el_aarch32_fiq 0x700
check_vector_slot __vector_lower_el_aarch32_serror 0x780

log="${build}/qemu-smoke.log"
set +e
timeout "$timeout_seconds" "$qemu" \
    -machine virt,secure=off,virtualization=off,gic-version=3 \
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
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: EXCEPTION VECTORS' "$log" ||
    die "exception vector installation was not reported"
vbar_hex="$(sed -nE 's/^exception: vbar=0x([0-9a-f]{16})$/\1/p' "$log" |
    head -n 1)"
[[ "$vbar_hex" =~ ^[0-9a-f]{16}$ ]] ||
    die "VBAR_EL1 diagnostic is missing or malformed"
(( 0x${vbar_hex} == vector_table_value )) ||
    die "VBAR_EL1 was not installed with the vector-table base"
grep -Fxq 'gic: gicd=0x0000000008000000 size=0x00010000' "$log" ||
    die "GICv3 distributor range is missing or unexpected"
grep -Fxq 'gic: gicr=0x00000000080a0000 size=0x00f60000' "$log" ||
    die "GICv3 redistributor range is missing or unexpected"
grep -Fxq 'gic: redistributor=0x00000000080a0000 affinity=0x00000000' "$log" ||
    die "GICv3 boot redistributor was not matched by affinity"
grep -Fxq 'gic: gicd-ctlr=0x00000052' "$log" ||
    die "GICD_CTLR ARE_NS/EnableGrp1NS state is missing or unexpected"
grep -Fxq 'gic: sre=1 dib=1 dfb=1 pmr=0xf0 bpr1=0x3 eoimode=1 igrpen1=1' "$log" ||
    die "GICv3 CPU-interface configuration is missing or unexpected"
grep -Fxq 'gic: self-sgi=0x0000000f' "$log" ||
    die "reserved self-SGI ID is missing or unexpected"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: GIC READY' "$log" ||
    die "GICv3 initialization did not complete"
grep -Fxq 'gic: local-sgi=0x0000000f' "$log" ||
    die "local self-SGI registration ID is missing or unexpected"
grep -Fxq 'gic: local-timer=0x0000001e' "$log" ||
    die "local timer PPI registration ID is missing or unexpected"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: LOCAL IRQ REGISTERED' "$log" ||
    die "local IRQ registration marker is missing"
awk '
    /ASTIA AARCH64 VIRT BOOT: GIC READY/ { ready = NR }
    /ASTIA AARCH64 VIRT BOOT: LOCAL IRQ REGISTERED/ { registered = NR }
    /ASTIA AARCH64 VIRT BOOT: IRQ ENABLED/ { enabled = NR }
    END { exit !(ready && registered && enabled && ready < registered &&
                 registered < enabled) }
' "$log" || die "local IRQ registration is not ordered before IRQ enable"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: IRQ ENABLED' "$log" ||
    die "IRQ enable marker is missing"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: IRQ SELF-SGI' "$log" ||
    die "self-SGI trigger marker is missing"
grep -Fxq 'irq: iar=0x0000000f' "$log" ||
    die "IRQ handler did not acknowledge the reserved SGI"
grep -Fxq 'irq: eoi=0x0000000f' "$log" ||
    die "IRQ handler did not write EOIR1 for the reserved SGI"
grep -Fxq 'irq: dir=0x0000000f' "$log" ||
    die "IRQ handler did not deactivate the reserved SGI"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: IRQ HANDLED' "$log" ||
    die "IRQ handler completion marker is missing"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: IRQ RETURNED' "$log" ||
    die "IRQ handler did not return to the interrupted context"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: GIC SELF-SGI OK' "$log" ||
    die "GICv3 self-SGI verification did not complete"
grep -Fxq 'timer: ppi=0x0000001e' "$log" ||
    die "timer PPI ID diagnostic is missing or unexpected"
grep -Fxq 'timer: frequency=0x03b9aca0 ticks=0x00098968' "$log" ||
    die "timer frequency/one-shot interval diagnostic is missing or unexpected"
grep -Eq '^timer: ctl=0x0000000[15] enable=1 imask=0 istatus=[01]$' "$log" ||
    die "timer CNTP_CTL_EL0 state is missing or unexpected"
grep -Fxq 'timer: tick-budget=0x00000005' "$log" ||
    die "timer periodic tick budget diagnostic is missing or unexpected"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: TIMER ARMED' "$log" ||
    die "timer arm marker is missing"
for irq_line in 'irq: iar=0x0000001e' 'irq: eoi=0x0000001e' \
    'irq: dir=0x0000001e' 'irq: timer=0x0000001e' \
    'ASTIA AARCH64 VIRT BOOT: TIMER HANDLED'; do
    [[ "$(grep -Fxc "$irq_line" "$log")" -eq 5 ]] ||
        die "expected five '$irq_line' lines"
done
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: TIMER RETURNED' "$log" ||
    die "timer IRQ handler did not return to the interrupted context"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: TIMER TICK OK' "$log" ||
    die "periodic timer tick verification did not complete"
awk '
    /ASTIA AARCH64 VIRT BOOT: TIMER ARMED/ { armed = NR; next }
    /irq: iar=0x0000001e/ {
        if (state != 0 || handled >= 5) bad = 1
        if (!first_ack) first_ack = NR
        state = 1
        next
    }
    /irq: eoi=0x0000001e/ {
        if (state != 1) bad = 1
        state = 2
        next
    }
    /irq: dir=0x0000001e/ {
        if (state != 2) bad = 1
        state = 3
        next
    }
    /irq: timer=0x0000001e/ {
        if (state != 3) bad = 1
        state = 4
        next
    }
    /ASTIA AARCH64 VIRT BOOT: TIMER HANDLED/ {
        if (state != 4) bad = 1
        handled++
        last_handled = NR
        state = 0
        next
    }
    /ASTIA AARCH64 VIRT BOOT: TIMER RETURNED/ {
        if (state != 0) bad = 1
        returned = NR
        next
    }
    /ASTIA AARCH64 VIRT BOOT: TIMER TICK OK/ {
        if (state != 0) bad = 1
        tick_ok = NR
        next
    }
    /ASTIA AARCH64 VIRT BOOT: EXCEPTION SYNC/ { brk = NR; next }
    END {
        exit !(armed && first_ack && returned && tick_ok && brk &&
               handled == 5 && !bad && state == 0 &&
               armed < first_ack && last_handled < returned &&
               returned < tick_ok && tick_ok < brk)
    }
' "$log" || die "timer proof markers are out of order"
grep -Fxq 'deadline: capacity=0x00000008' "$log" ||
    die "deadline capacity diagnostic is missing or unexpected"
grep -Fxq 'deadline: scheduled=0x00000003' "$log" ||
    die "deadline scheduled diagnostic is missing or unexpected"
grep -Fxq 'deadline: canceled=0x00000001' "$log" ||
    die "deadline canceled diagnostic is missing or unexpected"
[[ "$(grep -Fxc 'deadline: fired-a=1 fired-b=1 fired-c=0' "$log")" -eq 1 ]] ||
    die "deadline fired proof was not reported exactly once"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: DEADLINE OK' "$log" ||
    die "deadline verification did not complete"
awk '
    /ASTIA AARCH64 VIRT BOOT: TIMER TICK OK/ { tick = NR }
    /ASTIA AARCH64 VIRT BOOT: DEADLINE OK/ { deadline = NR }
    /ASTIA AARCH64 VIRT BOOT: EXCEPTION SYNC/ { brk = NR }
    END {
        exit !(tick && deadline && brk && tick < deadline && deadline < brk)
    }
' "$log" || die "deadline proof markers are out of order"
grep -Fxq 'exception: vector=current-el-spx-sync' "$log" ||
    die "synchronous exception did not enter the current-EL SPx slot"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: EXCEPTION SYNC' "$log" ||
    die "synchronous exception marker was not reported"
grep -Fxq 'exception: esr=0x00000000f2001234 ec=0x3c il=1 iss=0x00001234' "$log" ||
    die "BRK64 ESR_EL1 syndrome is missing or unexpected"
grep -Fxq 'exception: brk64 imm=0x1234' "$log" ||
    die "BRK64 immediate was not reported"
elr_hex="$(sed -nE 's/^exception: elr=0x([0-9a-f]{16})$/\1/p' "$log" |
    head -n 1)"
[[ "$elr_hex" =~ ^[0-9a-f]{16}$ ]] ||
    die "ELR_EL1 diagnostic is missing or malformed"
(( 0x${elr_hex} == brk_probe_value )) ||
    die "ELR_EL1 does not point at the deliberate BRK probe"
far_hex="$(sed -nE 's/^exception: far=0x([0-9a-f]{16})$/\1/p' "$log" |
    head -n 1)"
[[ "$far_hex" =~ ^[0-9a-f]{16}$ ]] ||
    die "FAR_EL1 diagnostic is missing or malformed"
[[ "$far_hex" == '5a5a5a5a5a5a5a5a' ]] ||
    die "FAR_EL1 was not captured from the seeded value"
grep -Eq '^exception: spsr=0x[0-9a-f]{16}$' "$log" ||
    die "SPSR_EL1 diagnostic is missing or malformed"
grep -Fq '[PANIC] process unavailable' "$log" ||
    die "panic report did not mark process context unavailable"
grep -Fq '[PANIC] fault vector-kind=4' "$log" ||
    die "panic report did not include the exception vector kind"
grep -Eq '^\[PANIC\]  x0:[[:space:]]+0x[0-9a-f]+' "$log" ||
    die "panic report did not include x0"
grep -Eq '^\[PANIC\]  sp:[[:space:]]+0x[0-9a-f]+' "$log" ||
    die "panic report did not include the interrupted SP"
grep -Fq '[PANIC] stack trace:' "$log" ||
    die "panic report did not include a stack trace"
grep -Fxq 'ASTIA AARCH64 VIRT BOOT: EXCEPTION HALT' "$log" ||
    die "synchronous exception did not halt cleanly"

printf '%s\n' 'aarch64 virt kernel smoke: PASS'
