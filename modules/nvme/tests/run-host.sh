#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${NVME_HOST_TEST_BUILD:-${TMPDIR:-/tmp}/astia-nvme-host-test}
rm -rf "$out"
mkdir -p "$out"
"${ASTIA_HOST_CXX_COMPILER:-${CXX:-c++}}" -std=c++20 -Wall -Wextra -Werror -pedantic \
    ${ASTIA_HOST_SANITIZER_FLAGS:-} \
    -I"$root" -I"$root/kernel/include" "$root/modules/nvme/tests/nvme_protocol_test.cpp" \
    ${ASTIA_HOST_SANITIZER_FLAGS:-} \
    -o "$out/nvme_protocol_test"
"$out/nvme_protocol_test"
