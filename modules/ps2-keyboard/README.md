# PS/2 keyboard module

This module owns the first 8042 keyboard port, decodes untranslated set-2
scan codes, applies a private US layout and modifier/lock state, and forwards
mapped terminal bytes through `astia::module::tty_input_write` into ttv0.

It is loaded as an ET_REL member of the boot initrd. There is no kernel init
step and no `hw::drivers` or filesystem registration for this device.

## Lifetime

The module is deliberately boot-lifetime resident and non-unloadable, and the
kernel owns the lifetime reference:

- `module_main` completes controller/keyboard bring-up, then calls
  `astia::module::ps2_claim_irq(handler)`. The kernel resolves the calling
  module, takes an active reference on it, and binds a guarded IRQ1 route whose
  enter/leave callbacks acquire and release that reference. There is no
  module-side lease object.
- The module then calls `astia::module::make_resident()`, which resolves the
  caller and marks it permanently non-unloadable. A subsequent `request_exit()`
  returns the resident result, keeps the image mapped, and skips fini/unmap.
- If the claim fails, no reference or handler is retained. If bring-up fails
  after the claim, `ps2_release_irq()` masks IRQ1, retries unregistering the
  route, drops the kernel's reference, and the image is left clean.
- All controller, decoder, keymap and modifier state is `static constinit`
  and lives for the system lifetime.

The module never releases the route because IRQ1 remains registered for the
system lifetime. Close waiters that become ready in interrupt context are
parked and delivered by the kernel's normal-context pump, never inline.

Module images are reached through the kernel direct map
(`kernel_base + physical`), which is shared into process address spaces. That
is required here because IRQ1 can arrive while a userspace process has its
address space active.

## Hardware contract

- Controller translation is disabled; the keyboard is reset, selects scan-code
  set 2, and is explicitly enabled for scanning with `F4`.
- The keyboard interface is disabled (`AD`) before the command byte is
  captured, then the aux port is disabled (`A7`) and the captured value is
  cross-checked. The original aux-enable bit is preserved, so an enabled PS/2
  mouse is restored rather than silently disabled.
- Config replies are accepted only when the reserved bits are clear and the
  inhibited ports report disabled, so an in-flight scan byte is drained, never
  stored as the command byte.
- The controller self-test (`AA -> 55`) and first-port test (`AB -> 00`) run
  with both ports disabled. A self-test that resets the command byte is
  followed by reinstating and verifying the inhibited configuration before the
  port test. The preserved config is written back and verified before the
  keyboard interface is re-enabled.
- `F0 02` and `F4` require `FA` under the same bounded resend policy as the
  reset. Status waits are bounded; `FE` (resend) is retried at most three
  times. Keyboard reset accepts `FA` and `AA` in either order.
- The IRQ handler is claimed only after controller and keyboard bring-up. PIC IRQ1 stays masked; the controller IRQ bit is enabled next,
  the module reports ready, and IRQ1 is unmasked last.
- The ISR drains a bounded number of output-buffer bytes and filters
  auxiliary bytes, parity/timeout failures and non-scan-code protocol bytes.
- No unrestricted `pio_bus` or `device_pio` is exposed. The module sees only
  the `astia::dev::ps2_port_window` returned by
  `astia::module::ps2_platform_ports()`, which permits byte accesses to ports
  `0x60` and `0x64` and rejects every other port.

## Input scope

The decoder covers set-2 make/break, `E0`/`E1` prefixes, Pause, PrintScreen,
fake shifts and error recovery. The keymap covers US letters, digits,
punctuation, modifiers, caps lock, terminal control bytes, navigation,
function keys and the keypad.

Backspace maps to `0x7f`, Enter to `CR`, and Ctrl combinations to their
control bytes. Dead keys, international layouts and media keys are out of
scope for this slice.

## Tests

Host units cover the fake 8042 controller ordering/configuration/self-test,
ACK/RESEND/BAT ordering and bounds, protocol-response separation, the guarded
port window, set-2 decoding, keymap/modifier behavior and error recovery.
`tests/check-imports.sh` validates that the module imports only the approved
public platform/sink/log symbols.

`astia_ps2_keyboard_qemu` builds a GRUB Multiboot2 ISO with the module and a
ttv0 reader in the initrd, boots it with `qemu-system-x86_64`, waits for the
module-ready and ttv0-probe markers, injects `a`, `b`, `c`, Enter through QMP,
and requires `PS2_KEYS:abc` from the ttv0 reader. No USB keyboard is attached.

```sh
cmake -S . -B build -G Ninja -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
cmake --build build --parallel
ctest --test-dir build -L ps2 --output-on-failure
```
