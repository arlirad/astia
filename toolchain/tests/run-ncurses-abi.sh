#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
package=${NCURSES_PACKAGE:-$root/build/user/ncurses/package}
sysroot=${ASTIA_SYSROOT:-$root/build/user/ncurses/cross/sysroot}
crt0=${ASTIA_CRT0:-$root/build/user/ncurses/cross/image/crt0.o}
build=${NCURSES_ABI_BUILD:-$root/build/user/ncurses/abi}
clang=${CLANG:-clang}
target=${ASTIA_TARGET:-x86_64-unknown-elf}
probe_source=${NCURSES_ABI_SOURCE:-$root/toolchain/tests/ncurses_abi_probe.c}
probe=$build/ncurses-abi-probe
evidence=$build/abi-evidence.txt

for input in "$package/include/curses.h" "$package/lib/libncurses.a" \
    "$sysroot/lib/libastia_libc.a" "$crt0" "$probe_source"; do
    if [ ! -f "$input" ]; then
        echo "ncurses ABI probe: missing input: $input" >&2
        exit 1
    fi
done

resource_dir=$("$clang" --target="$target" -print-resource-dir)
builtin_lib=$(find "$resource_dir" -name "libclang_rt.builtins-x86_64.a" -print -quit)
if [ -z "$builtin_lib" ]; then
    echo "ncurses ABI probe: clang compiler builtins not found" >&2
    exit 1
fi

mkdir -p "$build"
common_flags="--target=$target -U__linux__ -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource_dir/include -isystem $sysroot/include"

"$clang" $common_flags -I"$package/include" -c "$probe_source" \
    -o "$build/ncurses_abi_probe.o"
"$clang" $common_flags -nostdlib -nostartfiles -nodefaultlibs -static \
    -Wl,-no-pie -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 \
    -o "$probe" "$crt0" "$build/ncurses_abi_probe.o" \
    "$package/lib/libncurses.a" "$sysroot/lib/libastia_libc.a" "$builtin_lib"

undefined=$(nm -u "$probe" | wc -l | tr -d ' ')
if [ "$undefined" != 0 ]; then
    nm -u "$probe" >&2
    echo "ncurses ABI probe: unresolved symbols in linked probe" >&2
    exit 1
fi
if ! readelf -h "$probe" | grep -q 'Type:.*EXEC'; then
    echo "ncurses ABI probe: linked probe is not a static executable" >&2
    exit 1
fi
if readelf -l "$probe" | grep -q 'INTERP'; then
    echo "ncurses ABI probe: linked probe has a dynamic interpreter" >&2
    exit 1
fi
if readelf -d "$probe" 2>/dev/null | grep -q 'NEEDED'; then
    echo "ncurses ABI probe: linked probe has shared-library dependencies" >&2
    exit 1
fi
for symbol in newterm set_term cbreak noecho waddstr wrefresh wgetch endwin \
    delscreen tgetent tgetstr _nc_fallback; do
    if ! nm "$probe" | grep -q " T $symbol\$"; then
        echo "ncurses ABI probe: missing linked symbol: $symbol" >&2
        exit 1
    fi
done
if cmp -s "$package/lib/libncurses.a" "$package/lib/libcurses.a"; then
    alias=identical
else
    echo "ncurses ABI probe: libcurses.a differs from libncurses.a" >&2
    exit 1
fi

{
    printf '%s\n' "undefined=0"
    printf '%s\n' "elf_type=EXEC"
    printf '%s\n' "interp=none"
    printf '%s\n' "needed=none"
    printf '%s\n' "libcurses=identical"
    for symbol in newterm set_term cbreak noecho waddstr wrefresh wgetch endwin \
        delscreen tgetent tgetstr _nc_fallback; do
        printf 'symbol.%s=T\n' "$symbol"
    done
} >"$evidence"
while IFS= read -r line; do
    if ! grep -qx "$line" "$evidence"; then
        echo "ncurses ABI probe: evidence mismatch: $line" >&2
        exit 1
    fi
done <<EOF
undefined=0
elf_type=EXEC
interp=none
needed=none
libcurses=identical
symbol.newterm=T
symbol.set_term=T
symbol.cbreak=T
symbol.noecho=T
symbol.waddstr=T
symbol.wrefresh=T
symbol.wgetch=T
symbol.endwin=T
symbol.delscreen=T
symbol.tgetent=T
symbol.tgetstr=T
symbol._nc_fallback=T
EOF

printf '%s\n' "ncurses ABI probe: PASS ($probe)"
printf '%s\n' "ncurses ABI evidence: $evidence"
