#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build=${NVME_BUILD:-$root/build-nvme}
image=${NVME_IMAGE:-$root/modules/nvme/fixture.raw}
timeout_seconds=${NVME_TIMEOUT:-40}

run_case() {
    label=$1
    case_build=$2
    force_polling=$3
    case_log=${NVME_LOG:-${TMPDIR:-/tmp}/astia-nvme-$label.log}
    staging=${case_build}/nvme-initrd
    initrd=${case_build}/nvme-initrd.cpio
    case_image=${case_build}/fixture.raw

    cmake -S "$root" -B "$case_build" -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt \
        -DASTIA_RUNTIME_SKIP_LIFECYCLE=ON -DASTIA_NVME_FORCE_POLLING="$force_polling"
    cmake --build "$case_build" --target astiakrn_runtime nvme_module
    cp "$image" "$case_image"
    rm -rf "$staging"
    mkdir -p "$staging"
    cp "$case_build/modules/nvme/nvme.o" "$staging/nvme.o"
    (cd "$staging" && find nvme.o -print | cpio -o -H newc > "$initrd")

    set +e
    timeout "$timeout_seconds" qemu-system-x86_64 \
        -machine q35 -m 128M -kernel "$case_build/kernel/src/astiakrn_runtime" \
        -initrd "$initrd" \
        -drive "if=none,format=raw,file=$case_image,id=nvme0" \
        -device nvme,drive=nvme0,serial=ASTIA-NVME \
        -serial "file:$case_log" -display none -monitor none -device isa-debug-exit -no-reboot
    status=$?
    set -e

    cat "$case_log"
    test "$status" -eq 1
    grep -qF 'nvme: namespace blocks=' "$case_log"
    grep -qF 'nvme: known sector ok' "$case_log"
    if test "$force_polling" = OFF; then
        grep -qF 'nvme: msix enabled' "$case_log"
        grep -qF 'nvme: msix interrupt path' "$case_log"
        grep -qF 'nvme: async completion' "$case_log"
    else
        grep -qF 'nvme: polling fallback' "$case_log"
        ! grep -qF 'nvme: msix enabled' "$case_log"
        grep -qF 'nvme: async polling fallback' "$case_log"
    fi
    grep -qF 'nvme: async error path' "$case_log"
    grep -qF 'nvme: write flush reopen ok' "$case_log"
}

run_case msix "$build-msix" OFF
run_case polling "$build-polling" ON
