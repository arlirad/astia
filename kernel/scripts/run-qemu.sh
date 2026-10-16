#!/usr/bin/env sh
#
# Build the astiakernel kernel and boot it under QEMU with -kernel.
#
# The kernel uses the x86/HVM (PVH) direct boot ABI via a .note.Xen ELF
# note, which is what lets QEMU load a 64-bit ELF with -kernel (QEMU's
# multiboot loader only accepts i386 images).
#
set -eu

# Kernel source root = this script's parent's parent, so the script works from
# any working directory.
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

ARCH="${ARCH:-x86_64}"
BUILD="${BUILD:-$root/build}"

cmake -S "$root" -B "$BUILD" -DARCH="$ARCH"
cmake --build "$BUILD"

exec qemu-system-x86_64 \
    -kernel "$BUILD/src/astiakrn" \
    -serial stdio \
    -display none \
    -monitor none \
    -no-reboot
