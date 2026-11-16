# USB xHCI HID keyboard module

This directory contains a bounded C++ USB host stack and an ET_REL xHCI PCI
driver. `usb_protocol.hpp` owns the typed host-side USB model: little-endian
descriptor decoding, bounded configuration parsing, HID descriptor validation,
interface and endpoint validation, enumeration state transitions,
generation-checked transfer lifetime, standard setup requests, and a fixed-size
interface driver registry. `hid_keyboard.hpp` selects HID boot keyboard
interfaces, requests boot protocol, decodes eight-byte boot reports, rejects
malformed and rollover reports without inventing key events, and maps usage
transitions to tty bytes and ANSI controls.

`xhci.hpp` exposes setup-packet, TRB, control-ring, interrupt-ring,
asynchronous cancellation, deferred interrupt, and enumeration session APIs.
The xHCI module enables bus mastering, allocates page-aligned DMA rings and
contexts, resets a port, enables a slot, performs Address Device and control
transfers, configures a HID interrupt endpoint, requests boot protocol, and
routes decoded input through the kernel's selected `/dev/ttv0` backend. The
serial fallback remains independent.

The parser rejects truncated and zero-length descriptors, invalid standard
lengths, malformed HID descriptors, endpoint zero descriptors, reserved
endpoint bits, invalid transfer attributes, duplicate endpoints, inconsistent
interface endpoint counts, and missing SuperSpeed companion descriptors.
Unknown class and vendor descriptors are retained in the byte stream but
otherwise ignored.

Build the ET_REL object and host tests with CMake. The host test includes a
synthetic xHCI register image and exercises setup, TRB, ring, cancellation,
descriptor, keyboard lifecycle, report, keymap, modifier, and rollover paths.
`usb_qemu_keyboard_vga_smoke` verifies the QEMU xHCI, `usb-kbd`, and VGA
topology. Set `ASTIA_USB_VGA_ISO` to additionally boot an ISO and deliver
sample keys through QMP.
