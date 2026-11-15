# `/dev/ttv0`

`/dev/ttv0` is a first-class virtual terminal device. It has its own tty line
discipline state, input queue, readiness notifications, termios, and
controlling-terminal and foreground-process-group state. Opening it creates a
new open-file description, while the terminal state is shared by opens of the
same device as it is for a physical tty.

The device is deliberately not an alias for `/dev/tts0` or `/dev/console`.
`/dev/tts0` remains the UART tty, `/dev/console` remains the explicitly
registered console file, and `/dev/ttv0` is a separate tty whose output is sent
through the active console backend. This keeps standard streams and UART
behavior unchanged while making console selection explicit.

The backend contract is byte-oriented. On the Multiboot2 boot path, `ttv0` is
bound to the discovered linear framebuffer after paging is ready. The public
descriptor carries one physical address, width, height, pitch, and pixel
format. The implementation currently accepts only 32-bit direct-color RGBX or
BGRX layouts; indexed, text-mode, packed-pixel, and multiple-framebuffer boot
descriptors are not supported. The framebuffer range is validated and mapped
through the kernel MMIO mapping API, and the mapping is owned by the backend
for the lifetime of the device.

The renderer uses the AEX2 PSF1 font layout (the bundled Spleen PSF asset;
see `docs/NOTICE` for its BSD license and attribution),
8-pixel glyph cells, ANSI SGR palette attributes, cursor movement, and
scrolling. Keyboard drivers can route bytes through the existing
`virtual_tty::inject_input` path; no separate input protocol is introduced.

The PVH QEMU `-kernel` path currently supplies no framebuffer descriptor. In
that case framebuffer discovery succeeds without claiming a device and
`/dev/ttv0` is not registered; it never guesses VGA memory or dereferences an
unadvertised address. `/dev/tts0` and `/dev/console` remain the UART console.
The Multiboot2 header carries an optional framebuffer preference and the GRUB
configuration requests a graphical payload. The host QEMU/GRUB smoke harness
used by this tree currently does not expose a framebuffer tag even with a
graphical display backend, so its smoke test asserts the explicit
`framebuffer: unavailable` fallback. The parser and renderer tests inject a
real Multiboot2 descriptor and fake pixel surface; on a bootloader that emits
tag 8, the runtime marker is `framebuffer: ready` and `/dev/ttv0` is bound to
the mapped surface.

If no active console backend is registered, virtual-terminal initialization
fails and writes do not report the device as writable.
