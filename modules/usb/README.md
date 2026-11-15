# USB foundation module

This directory contains a bounded C++ USB foundation and an ET_REL xHCI PCI
probe skeleton. `usb_protocol.hpp` owns the typed host-side USB model: little-
endian descriptor decoding, bounded configuration parsing, interface and
endpoint validation, enumeration state transitions, generation-checked
transfer lifetime, and a fixed-size interface driver registry.

`xhci.hpp` reads only safely bounded capability and operational registers. The
xHCI module identifies class `0c/03/30` PCI functions and checks the mapped BAR,
CAPLENGTH, structural parameters, controller readiness, and 4 KiB page support.
It never writes controller registers, allocates rings, enables DMA or
interrupts, enumerates a port, or submits a transfer. A valid probe therefore
reports the explicit `unsupported_no_transfer_engine` state and returns
`-95`; this is not keyboard, hub, storage, or class-driver support.

The parser rejects truncated and zero-length descriptors, invalid standard
lengths, endpoint zero descriptors, reserved endpoint bits, invalid transfer
attributes, duplicate endpoints, inconsistent interface endpoint counts, and
missing SuperSpeed companion descriptors. Unknown class and vendor
descriptors are retained in the byte stream but otherwise ignored.

Build the ET_REL object and host tests with CMake. The host test includes a
synthetic xHCI register image as the explicit capability test. When QEMU is
available, `usb_qemu_capability_smoke` verifies that QEMU can instantiate its
xHCI device; it does not claim hardware enumeration.
