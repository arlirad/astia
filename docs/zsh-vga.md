# zsh through the VGA virtual terminal

`toolchain/run-zsh-vga-qemu.sh` is the graphical sibling of
`toolchain/run-zsh-qemu.sh`. It performs the same kernel preflight, zsh and
Astia libc build, coreutils packaging, optional nano packaging, clean-build
handling, and line-reader selection. The generated initrd starts zsh with
standard streams opened on `/dev/ttv0`; the framebuffer renderer is therefore
the shell's output device rather than an alias for the UART console.

The launcher builds a GRUB Multiboot2 ISO containing the runtime kernel and
initrd. Its GRUB entry requests `1024x768x32` and `gfxpayload=keep`, and QEMU
uses `-vga std` with a graphical display backend. Kernel diagnostics remain on
the separate COM1 serial channel. UART input is also forwarded to the active
`ttv0` line discipline so a serial harness can drive the graphical shell
without pretending that VGA memory is a keyboard device.

The default is strict: the launcher expects both `framebuffer: ready` and
`ttv0: selected`. If GRUB/QEMU emits no Multiboot2 framebuffer tag, it stops
with an actionable error. It never guesses a VGA address. To intentionally
exercise the documented UART-only fallback, use:

```sh
ASTIA_VGA_FALLBACK=serial ASTIA_ZSH_VGA_HARNESS=1 \
  toolchain/run-zsh-vga-qemu.sh
```

The fallback expects `framebuffer: unavailable` and uses the ordinary UART
zsh runner. Its harness uses QEMU's PVH `-kernel`/`-initrd` path rather than
the graphical GRUB ISO, so the absence of a framebuffer descriptor is tested
truthfully. It is not a successful VGA test.

Set `ASTIA_VGA_STRICT=1` for a graphical smoke that additionally requires the
host's 800x600 24-bit BGR descriptor and verifies an 800x600 screenshot with
the green console marker. Strict mode cannot be combined with the serial
fallback.

## Interactive launch

```sh
toolchain/run-zsh-vga-qemu.sh
```

The graphical QEMU window is the terminal. The launcher verifies the boot
marker before forwarding host input to COM1. Override the display backend and
paths when needed:

```sh
ASTIA_VGA_DISPLAY=gtk \
ASTIA_VGA_SERIAL_LOG=/tmp/astia-vga-serial.log \
ZSH_SOURCE=/tmp/zsh ZSH_BUILD=/tmp/astia-zsh-build \
  toolchain/run-zsh-vga-qemu.sh
```

The repository-level CMake project exposes the same launcher as the
`astia_vga_qemu` target. The standalone launcher and CMake target both default
to two virtual CPUs and 256 MiB of memory; CMake keeps launcher paths in its
build tree:

```sh
cmake -S . -B build
cmake --build build --target astia_vga_qemu
```

Override the QEMU executable, resources, or optional nano image at configure
time, for example:

```sh
cmake -S . -B build \
  -DASTIA_VGA_QEMU=/opt/qemu/bin/qemu-system-x86_64 \
  -DASTIA_VGA_MEMORY=512M -DASTIA_VGA_CPUS=4 \
  -DASTIA_VGA_NANO=ON
```

The target also accepts `ASTIA_VGA_KERNEL_BUILD`, `ASTIA_VGA_ZSH_BUILD`,
`ASTIA_VGA_NANO_BUILD`, and `ASTIA_VGA_RUN` cache paths. Set
`ASTIA_VGA_HARNESS=ON` for the smoke harness, or
`ASTIA_VGA_FALLBACK=serial` to exercise the documented UART-only fallback.

`ASTIA_VGA_DISPLAY=none` is useful only for diagnostics or the explicit serial
fallback; it is not expected to provide a framebuffer descriptor.

## Smoke harness

The opt-in harness starts QEMU with a serial PTY and HMP monitor, checks the
framebuffer marker and `/dev/ttv0` runner marker, captures the framebuffer with
QEMU `screendump`, and uses ImageMagick plus Tesseract to check the colored VGA
prompt and zsh command output. It also checks `pwd`; `ASTIA_LINE_READER=1`
switches the smoke to the framebuffer line-reader path.

```sh
ASTIA_ZSH_VGA_HARNESS=1 toolchain/run-zsh-vga-qemu.sh
ASTIA_ZSH_VGA_HARNESS=1 ASTIA_LINE_READER=1 \
  toolchain/run-zsh-vga-qemu.sh
```

The nano path remains opt-in and reuses the existing build and iteration
overrides:

```sh
ASTIA_NANO=1 ASTIA_NANO_HARNESS=1 ASTIA_ZSH_VGA_HARNESS=1 \
  toolchain/run-zsh-vga-qemu.sh
```

The smoke harness requires `python3`, `convert`, `identify`, and `tesseract` for a real
framebuffer run. It stores the serial log, HMP screenshot, and byte trace in
`ASTIA_ZSH_VGA_RUN` (or the paths selected by the corresponding overrides).
