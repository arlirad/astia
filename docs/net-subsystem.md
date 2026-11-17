# Network subsystem contract

Astia network interfaces are first-class `net` devices. They are not
filesystem nodes, do not inherit `fs::file`, and are never passed to
`fs::register_device` or exposed below `/dev`. A socket or future network
stack consumes the registry in `<astia/net.hpp>` directly.

## Public boundary

The C layout in `<astia/module_abi_net.h>` is the stable module boundary.
The C++ API in `<astia/net.hpp>` supplies queue, callback, registry, and
lifetime helpers. Pointers are 64-bit, lengths are byte counts, and the ABI
page size is `ASTIA_PAGE_SIZE` (4096 on the supported x86-64 target). Device
register access remains driver-specific; RTL8139 uses typed offsets and
little-endian accessors in its protocol header. Network metadata contains a
stable name, MAC address, MTU, link flags, and a monotonically changing
generation.

## Packet ownership

An RX `packet_view` is borrowed. Its segments and recycle token are valid only
until the callback returns. Returning `defer` transfers the view to the
bounded deferred path; the process-context poll callback must not retain it
after returning. A full deferred queue drops the packet and recycles it.
Consumers needing a longer lifetime copy the segments into their own buffer.

A TX request transfers ownership only when submission returns zero. The
reclaim callback runs once, in process context, after completion, cancellation,
or reset. `-EAGAIN` means the bounded queue was full and ownership was not
transferred.

## Context rules

The RX immediate callback runs in IRQ context. It must not allocate, sleep,
take a blocking lock, enter the filesystem, invoke arbitrary stack code, or
retain a view. It may return `recycle`, `drop`, or `defer`. Deferred RX and TX
reclaim callbacks run from process context and may copy data or perform stack
work. IRQ handlers acknowledge hardware and enqueue records only; they never
run reclaim or deferred callbacks.

The RX path is a bounded lock-free SPSC queue: the interrupt producer is the
only producer and the deferred poller is the only consumer. TX submission is
a bounded lock-free MPSC queue, allowing producers on multiple CPUs, with a
single controller service consumer. Completion delivery uses a bounded SPSC
queue so IRQ code remains nonblocking. Queue exhaustion is reported as
`-EAGAIN` or causes RX drop/recycle according to the path.

## Lifetime and reset

Each accepted TX request is tagged with the current device generation. Reset
first masks/disables interrupts, drains pending and in-flight ownership with
`-ECANCELED`, advances the generation, and then reinitializes hardware.
Detach unregisters the net device, drains borrowed RX state, cancels TX, and
releases DMA only after no callback can observe the old generation. A stale
completion or request from an earlier generation is rejected.

## Discovery

Modules register a PCI net driver with
`astia_module_register_pci_net_driver`. PCI discovery invokes its match and
probe callbacks; a successful probe registers an
`astia_module_net_device` with `astia_module_register_net_device`. Removal
unregisters that object before the driver context is destroyed. This path
uses the net registry only and has no device-file adapter.

RTL8139 currently provides the C-mode ring/parser, bounded paths, reset and
cancel behavior, and QEMU device-creation smoke. It does not claim a TCP/IP
stack or a packet-delivery integration beyond the net subsystem callbacks.
