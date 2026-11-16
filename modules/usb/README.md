# USB foundation module

This directory contains a bounded C++ USB foundation and an ET_REL xHCI PCI
control-transfer enumerator. `usb_protocol.hpp` owns the typed host-side USB
model: little-endian descriptor decoding, bounded configuration parsing,
interface and endpoint validation, enumeration state transitions,
generation-checked transfer lifetime, standard setup requests, and a
fixed-size interface driver registry.

`xhci.hpp` reads safely bounded capability and operational registers and exposes
setup-packet, TRB, control-ring, asynchronous cancellation, and enumeration
session APIs. `enumerate_pci_port` enables PCI bus mastering, resets a supported
xHCI controller, allocates page-aligned DMA rings and contexts, resets a port,
enables a slot, performs the xHCI Address Device sequence, submits control
transfers, and parses the device and first configuration descriptors. The
capability-only probe remains side-effect free and reports
`unsupported_no_transfer_engine`; the explicit enumeration entry point owns the
hardware start path.

The parser rejects truncated and zero-length descriptors, invalid standard
lengths, endpoint zero descriptors, reserved endpoint bits, invalid transfer
attributes, duplicate endpoints, inconsistent interface endpoint counts, and
missing SuperSpeed companion descriptors. Unknown class and vendor
descriptors are retained in the byte stream but otherwise ignored.

Build the ET_REL object and host tests with CMake. The host test includes a
synthetic xHCI register image as the capability contract and exercises setup,
TRB, ring, cancellation, and descriptor-enumeration helpers. When QEMU is
available, `usb_qemu_enumeration_smoke` instantiates QEMU xHCI with a USB
keyboard attached. This verifies the controller/device topology only: the
current kernel module loader has no USB-controller registration hook, so the
smoke honestly does not claim a booted-device enumeration result. No keyboard,
hub, storage, or other class driver is provided.
