#!/bin/sh
# Build and run the host-side unit tests.
set -e

# Source root = this script's parent's parent, so it works from any directory;
# build trees land next to the source, under kernel/.
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

BUILD_DIR="${1:-$root/build-tests}"

cmake -S "$root" -B "$BUILD_DIR" -DARCH=x86_64 -DASTIA_BUILD_TESTS=ON
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
