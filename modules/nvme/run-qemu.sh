#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${NVME_BUILD:-$root/build-nvme}
case "$build" in
    /*) ;;
    *) build=$root/$build ;;
esac
artifact_build=${ASTIA_TEST_BUILD:-$build}
case "$artifact_build" in
    /*) ;;
    *) artifact_build=$root/$artifact_build ;;
esac
image=${NVME_IMAGE:-$root/modules/nvme/fixture.raw}
timeout_seconds=${NVME_TIMEOUT:-40}
qemu=${QEMU:-qemu-system-x86_64}
case=${NVME_CASE:-all}

run_case() {
    label=$1
    case_build=$2
    case "$case_build" in
        /*) ;;
        *) case_build=$root/$case_build ;;
    esac
    force_polling=$3
    case_log=${NVME_LOG:-${TMPDIR:-/tmp}/astia-nvme-$label.log}
    staging=${case_build}/nvme-initrd
    initrd=${case_build}/nvme-initrd.cpio
    case_image=${case_build}/fixture.raw
    namespace2=${case_build}/fixture-ns2.raw

    mkdir -p "$case_build"
    if test ! -f "$namespace2" || test "$(stat -c %s "$namespace2")" -ne 2097152; then
        dd if=/dev/zero of="$namespace2" bs=1M count=2 status=none
        printf 'ASTIA_NVME_NAMESPACE_002\n' | dd of="$namespace2" conv=notrunc status=none
    fi
    if [ -z "${ASTIA_TEST_BUILD:-}" ]; then
        cmake -S "$root" -B "$case_build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
            -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON -DASTIA_NVME_FORCE_POLLING="$force_polling"
        cmake --build "$case_build" --target astiakrn_runtime nvme_module
        kernel_build=$case_build
        kernel=$case_build/kernel/src/astiakrn_runtime
        if test "$force_polling" = ON; then
            module=$case_build/modules/nvme/nvme-polling.o
        else
            module=$case_build/modules/nvme/nvme-msix.o
        fi
    else
        cmake --build "$artifact_build" --target astiakrn_module_runtime "nvme_${label}_module"
        kernel_build=$artifact_build
        kernel=$artifact_build/kernel/src/astiakrn_module_runtime
        if test "$force_polling" = ON; then
            module=$artifact_build/modules/nvme/nvme-polling.o
        else
            module=$artifact_build/modules/nvme/nvme-msix.o
        fi
    fi
    cp "$image" "$case_image"
    rm -rf "$staging"
    mkdir -p "$staging/mod"
    cp "$module" "$staging/mod/nvme.o"
    (cd "$staging" && find mod -print | cpio -o -H newc > "$initrd")

    set +e
    timeout "$timeout_seconds" "$qemu" \
        -machine q35 -m 128M -kernel "$kernel" \
        -initrd "$initrd" \
        -drive "if=none,format=raw,file=$case_image,id=nvme0ns1" \
        -drive "if=none,format=raw,file=$namespace2,id=nvme0ns2" \
        -device nvme,id=nvme0,serial=ASTIA-NVME \
        -device nvme-ns,drive=nvme0ns1,bus=nvme0,nsid=1 \
        -device nvme-ns,drive=nvme0ns2,bus=nvme0,nsid=2 \
        -serial "file:$case_log" -display none -monitor none -device isa-debug-exit -no-reboot
    status=$?
    set -e

    cat "$case_log"
    test "$status" -eq 1
    grep -qF 'nvme: namespace count=2' "$case_log"
    grep -qF 'nvme: namespace 1 ok' "$case_log"
    grep -qF 'nvme: namespace 2 ok' "$case_log"
    grep -qF 'nvme: known sector ok' "$case_log"
    grep -qF 'nvme: write flush reopen ok' "$case_log"
    grep -qF 'nvme: controller reset' "$case_log"
    grep -qF 'nvme: controller recovery ok' "$case_log"
    grep -qF 'nvme: reset recovery persistence ok' "$case_log"
    grep -qF 'nvme: reset recovery error path' "$case_log"
    if test "$force_polling" = OFF; then
        grep -qF 'nvme: msix enabled' "$case_log"
        grep -qF 'nvme: msix interrupt path' "$case_log"
        grep -qF 'nvme: async completion' "$case_log"
    else
        grep -qF 'nvme: polling fallback' "$case_log"
        ! grep -qF 'nvme: msix enabled' "$case_log"
        grep -qF 'nvme: async polling fallback' "$case_log"
    fi
}

case "$case" in
    all)
        run_case msix "$build-msix" OFF
        run_case polling "$build-polling" ON
        ;;
    msix)
        run_case msix "$build-msix" OFF
        ;;
    polling)
        run_case polling "$build-polling" ON
        ;;
    *)
        echo "NVME_CASE must be all, msix, or polling" >&2
        exit 2
        ;;
esac
