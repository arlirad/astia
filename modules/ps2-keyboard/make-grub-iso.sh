#!/usr/bin/env sh
set -eu

build=${BUILD:?usage: BUILD=<dir> ISO=<iso> KERNEL=<kernel> MODULE=<module> make-grub-iso.sh}
iso=${ISO:?}
kernel=${KERNEL:?}
module=${MODULE:?}
source_dir=${SOURCE_DIR:?}
include_dir=${INCLUDE_DIR:?}
cc=${CC:?}
cpio=${CPIO:-cpio}
grub_file=${GRUB_FILE:-grub-file}
grub_mkrescue=${GRUB_MKRESCUE:-grub-mkrescue}

work=$build/ps2-initrd
stage=$build/ps2-grub
rm -rf "$work" "$stage"
mkdir -p "$work/sbin" "$work/bin" "$work/mod" "$stage/boot/grub"

compile_flags="-std=c11 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie
    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
    -fno-builtin -Wall -Wextra -Werror -pedantic -I$include_dir"
link_flags="-nostdlib -static -Wl,-no-pie -Wl,-e,_start -Wl,-Ttext-segment=0x40000000"

# shellcheck disable=SC2086
$cc $compile_flags -c "$source_dir/tests/ps2_ttv0_probe.c" -o "$work/probe.o"
# shellcheck disable=SC2086
$cc $link_flags -o "$work/sbin/init" "$work/probe.o"
# shellcheck disable=SC2086
$cc $compile_flags -c "$source_dir/tests/ps2_noop.c" -o "$work/noop.o"
# shellcheck disable=SC2086
$cc $link_flags -o "$work/bin/init_libc" "$work/noop.o"

cp "$module" "$work/mod/ps2-keyboard.km"
(cd "$work" && find mod sbin bin -print |
    "$cpio" -o --format=newc --quiet) >"$stage/boot/initrd.cpio"

cp "$kernel" "$stage/boot/astiakrn"
cat >"$stage/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
set gfxmode=1024x768x32
set gfxpayload=keep
insmod all_video
insmod gfxterm
terminal_output gfxterm

menuentry 'Astia PS/2 keyboard' {
    multiboot2 /boot/astiakrn
    module2 /boot/initrd.cpio
    boot
}
EOF
"$grub_file" --is-x86-multiboot2 "$kernel"
mkdir -p "$(dirname -- "$iso")"
"$grub_mkrescue" -o "$iso" "$stage" >/dev/null
test -s "$iso"
