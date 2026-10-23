#!/usr/bin/env sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD:-$root/build-grub}
iso=${ISO:-$build/astia-grub.iso}
kernel_target=${KERNEL_TARGET:-astiakrn}

cmake -S "$root" -B "$build" -DARCH=x86_64
cmake --build "$build" --target "$kernel_target"
stage=$build/grub
rm -rf "$stage"
mkdir -p "$stage/boot/grub"
cp "$build/src/$kernel_target" "$stage/boot/astiakrn"
cp "$root/grub.cfg" "$stage/boot/grub/grub.cfg"
grub-file --is-x86-multiboot2 "$build/src/$kernel_target"
grub-mkrescue -o "$iso" "$stage"
printf '%s\n' "$iso"
