#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${NVME_HOST_TEST_BUILD:-${TMPDIR:-/tmp}/astia-nvme-host-test}
rm -rf "$out"
mkdir -p "$out"

run_test() {
    name=$1

    "${ASTIA_HOST_CXX_COMPILER:-${CXX:-c++}}" -std=c++20 -Wall -Wextra -Werror -pedantic \
        -ffunction-sections -fdata-sections \
        ${ASTIA_HOST_SANITIZER_FLAGS:-} \
        -I"$root" -I"$root/kernel/include" "$root/modules/nvme/tests/$name.cpp" \
        -Wl,--gc-sections \
        ${ASTIA_HOST_SANITIZER_FLAGS:-} \
        -o "$out/$name"
    "$out/$name"
}

run_test nvme_protocol_test
run_test nvme_start_test
run_test nvme_teardown_test
run_test nvme_async_test
