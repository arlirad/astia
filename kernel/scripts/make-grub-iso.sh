#!/usr/bin/env sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD:-$root/build-grub}
iso=${ISO:-$build/astia-grub.iso}

cmake -S "$root" -B "$build" -DARCH=x86_64
cmake --build "$build" --target astiakrn
stage=$build/grub
rm -rf "$stage"
mkdir -p "$stage/boot/grub"
cp "$build/src/astiakrn" "$stage/boot/astiakrn"
cp "$root/grub.cfg" "$stage/boot/grub/grub.cfg"
grub-file --is-x86-multiboot2 "$build/src/astiakrn"
grub-mkrescue -o "$iso" "$stage"
printf '%s\n' "$iso"
