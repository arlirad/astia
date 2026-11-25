#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source=${POSIX_TEST_SOURCE:-}
build=${POSIX_TEST_BUILD:-/tmp/astia-posix-tests}
cross_build=${ASTIA_CROSS_BUILD:-/tmp/astia-clang-init-libc}
sysroot=${ASTIA_SYSROOT:-$cross_build/sysroot}
crt0=${ASTIA_CRT0:-$cross_build/image/crt0.o}
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
target=x86_64-unknown-elf
timeout_seconds=${POSIX_TEST_TIMEOUT:-90}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
base_initrd=${ASTIA_INITRD:-$root/kernel/build-tests/tests/fixtures/boot_initrd.cpio}
zsh_binary=${ZSH_BINARY:-${ZSH_BUILD:-/tmp/astia-zsh-build}/output/zsh}

usage() {
    echo "usage: $0 [--list | --test CATEGORY/NUMBER | --category CATEGORY]" >&2
}

command -v "$clang" >/dev/null 2>&1 || { echo "clang executable not found: $clang" >&2; exit 1; }
command -v "$qemu" >/dev/null 2>&1 || { echo "QEMU executable not found: $qemu" >&2; exit 1; }
command -v timeout >/dev/null 2>&1 || { echo "timeout command not found" >&2; exit 1; }
command -v git >/dev/null 2>&1 || { echo "git executable not found" >&2; exit 1; }
cpio=${ASTIA_CPIO:-cpio}
command -v "$cpio" >/dev/null 2>&1 || { echo "cpio executable not found: $cpio" >&2; exit 1; }

if [ -z "$source" ]; then
    echo "POSIX_TEST_SOURCE must point to a clean LTP checkout or its open_posix_testsuite directory" >&2
    exit 2
fi
source=$(CDPATH= cd -- "$source" && pwd)
if [ -f "$source/testcases/open_posix_testsuite/Makefile" ]; then
    ltp=$source
    suite=$source/testcases/open_posix_testsuite
elif [ -f "$source/Makefile" ] && [ -d "$source/conformance" ]; then
    suite=$source
    ltp=$(git -C "$source" rev-parse --show-toplevel)
else
    echo "not an LTP Open POSIX checkout: $source" >&2
    exit 2
fi
test -d "$ltp/.git" || { echo "source is not a git checkout: $ltp" >&2; exit 2; }
test -z "$(git -C "$ltp" status --porcelain)" || {
    echo "POSIX_TEST_SOURCE must be clean; refusing modified or untracked sources" >&2
    git -C "$ltp" status --short >&2
    exit 2
}
git -C "$ltp" diff --quiet || { echo "unstaged source changes detected" >&2; exit 2; }
git -C "$ltp" diff --cached --quiet || { echo "staged source changes detected" >&2; exit 2; }

test -x "$kernel" || { echo "Astia kernel runtime missing: $kernel" >&2; exit 1; }
test -f "$base_initrd" || { echo "Astia base initrd missing: $base_initrd" >&2; exit 1; }
if [ ! -f "$sysroot/lib/libastia_libc.a" ] || [ ! -f "$crt0" ]; then
    echo "Astia sysroot/startup missing; run toolchain/build-init-libc.sh first" >&2
    exit 1
fi

if [ -n "${ZSH_SOURCE:-}" ]; then
    zsh_source=$(CDPATH= cd -- "$ZSH_SOURCE" && pwd)
    test -d "$zsh_source/.git" || { echo "ZSH_SOURCE is not a git checkout: $zsh_source" >&2; exit 2; }
    test -z "$(git -C "$zsh_source" status --porcelain)" || {
        echo "ZSH_SOURCE must be clean; refusing modified or untracked sources" >&2
        exit 2
    }
fi
if [ ! -x "$zsh_binary" ] && [ -n "${ZSH_SOURCE:-}" ]; then
    ZSH_SOURCE="$ZSH_SOURCE" ZSH_BUILD="${ZSH_BUILD:-/tmp/astia-zsh-build}" \
        "$root/toolchain/build-zsh.sh"
fi
if [ ! -x "$zsh_binary" ]; then
    echo "Astia zsh missing: $zsh_binary" >&2
    echo "build an unpatched checkout with ZSH_SOURCE=/path/to/zsh toolchain/build-zsh.sh" >&2
    exit 1
fi

selection=default
selection_value=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --list)
            printf '%s\n' getpid getppid fork/4-1 fork/9-1 kill killpg \
                fsync/4-1 fsync/5-1 fsync/7-1
            exit 0
            ;;
        --test)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            selection=test
            selection_value=$2
            shift 2
            ;;
        --category)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            selection=category
            selection_value=$2
            shift 2
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

work=$build
rm -rf "$work"
mkdir -p "$work/obj" "$work/root/sbin" "$work/root/bin" "$work/logs"
(cd "$work/root" && "$cpio" -id --quiet --no-absolute-filenames) <"$base_initrd"
mkdir -p "$work/root/etc"
selected=$work/selected

case "$selection" in
    default)
        printf '%s\n' 'fork/4-1' 'fork/9-1' 'kill/2-1' 'killpg/1-1' \
            'fsync/4-1' 'fsync/5-1' 'fsync/7-1' >"$selected"
        ;;
    test)
        case "$selection_value" in
            conformance/interfaces/*) selection_value=${selection_value#conformance/interfaces/} ;;
        esac
        case "$selection_value" in
            */*|*..*) : ;;
            *) echo "test must be CATEGORY/NUMBER, got $selection_value" >&2; exit 2 ;;
        esac
        printf '%s\n' "$selection_value" >"$selected"
        ;;
    category)
        case "$selection_value" in
            ''|*/*|*..*) echo "category must be a direct interfaces directory" >&2; exit 2 ;;
        esac
        category_dir=$suite/conformance/interfaces/$selection_value
        test -d "$category_dir" || { echo "unknown category: $selection_value" >&2; exit 2; }
        find "$category_dir" -maxdepth 1 -type f -name '*.c' -printf '%f\n' |
            sed 's/\.c$//' | sort | sed "s#^#$selection_value/#" >"$selected"
        test -s "$selected" || { echo "category has no direct C tests: $selection_value" >&2; exit 2; }
        ;;
esac

resource=$($clang --target="$target" -print-resource-dir)
builtins=$resource/lib/linux/libclang_rt.builtins-x86_64.a
test -f "$builtins" || { echo "Clang builtins archive missing: $builtins" >&2; exit 1; }
common="--target=$target -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -nostdinc -isystem $resource/include -isystem $sysroot/include -I$suite/include -I$suite/conformance/interfaces/testfrmw"

$clang $common -c "$suite/lib/common.c" -o "$work/obj/common.o"

runner_args=
while IFS= read -r test_name; do
    test_source=$suite/conformance/interfaces/$test_name.c
    test_binary=$work/root/bin/posix-$(printf '%s' "$test_name" | tr / _)
    test -f "$test_source" || { echo "test source missing: $test_name" >&2; exit 2; }
    object=$work/obj/$(printf '%s' "$test_name" | tr / _).o
    $clang $common -c "$test_source" -o "$object" >"$work/logs/$(printf '%s' "$test_name" | tr / _).build.log" 2>&1 || {
        cat "$work/logs/$(printf '%s' "$test_name" | tr / _).build.log" >&2
        echo "POSIX build blocker: $test_name" >&2
        exit 1
    }
    $clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
        -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$test_binary" \
        "$crt0" "$object" "$work/obj/common.o" "$sysroot/lib/libastia_libc.a" "$builtins"
done <"$selected"

runner_object=$work/obj/posix-runner.o
$clang $common -c "$root/toolchain/posix-runner.c" -o "$runner_object"
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$work/root/sbin/init" \
    "$crt0" "$runner_object" "$sysroot/lib/libastia_libc.a" "$builtins"

cp "$zsh_binary" "$work/root/bin/zsh"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" COREUTILS_BUILD="$work/coreutils" \
    "$root/coreutils/build.sh" >"$work/logs/coreutils.build.log" 2>&1
cp "$work/coreutils/bin/"* "$work/root/bin/"
(cd "$work/root" && find sbin bin etc -print |
    "$cpio" -o --format=newc --quiet) >"$work/initrd.cpio"

failed=0
while IFS= read -r test_name; do
    test_id=$(printf '%s' "$test_name" | tr / _)
    printf '%s %s\n' "$test_name" "/bin/posix-$test_id" >"$work/root/etc/posix-tests"
    test_initrd=$work/initrd-$test_id.cpio
    (cd "$work/root" && find sbin bin etc -print |
        "$cpio" -o --format=newc --quiet) >"$test_initrd"
    log=$work/logs/$test_id.qemu.log
    set +e
    timeout "$timeout_seconds" "$qemu" -m 128M -kernel "$kernel" -initrd "$test_initrd" \
        -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
    qemu_status=$?
    set -e
    cat "$log"
    if [ "$qemu_status" -eq 124 ]; then
        printf 'POSIX_TEST %s FAIL timeout=%s\n' "$test_name" "$timeout_seconds" | tee -a "$log"
        failed=1
        continue
    fi
    if [ "$qemu_status" -ne 1 ]; then
        printf 'POSIX_TEST %s FAIL qemu_status=%s\n' "$test_name" "$qemu_status" | tee -a "$log"
        failed=1
        continue
    fi
    if ! grep -q '^POSIX_TEST .* \(PASS\|UNSUPPORTED\) ' "$log"; then
        printf 'POSIX_TEST %s FAIL no-result\n' "$test_name" | tee -a "$log"
        failed=1
        continue
    fi
    if grep -q '^POSIX_TEST .* FAIL ' "$log"; then
        failed=1
    fi
done <"$selected"

if [ "$failed" -ne 0 ]; then
    echo "POSIX tests failed (logs: $work/logs)" >&2
    exit 1
fi
echo "Astia POSIX results: $work/logs"
