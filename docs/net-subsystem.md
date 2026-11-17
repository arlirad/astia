# Network subsystem contract

Astia network interfaces are first-class `net` devices. They are not
filesystem nodes, do not inherit `fs::file`, and are never passed to
`fs::register_device` or exposed below `/dev`. A socket or future network
stack consumes the registry in `<astia/net.hpp>` directly.

## Public boundary

The C++ API in `<astia/net.hpp>` is the device boundary. `astia::net::netdev`
is a first-class `dev::device` with metadata, callback context, RX/TX ownership
paths, and generation state. `astia::net::registry` is the single network-device
registry. The PCI module ABI only supplies match, probe, and remove adapters;
probe returns the `astia::net::netdev` constructed by the module. Pointers
are 64-bit, lengths are byte counts, and the ABI page size is
`ASTIA_PAGE_SIZE` (4096 on the supported x86-64 target). Device register
access remains driver-specific; RTL8139 uses typed offsets and little-endian
accessors in its protocol header.

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

Modules register a PCI net driver with `astia::module::register_net_driver`.
PCI discovery invokes its match and probe callbacks; a successful probe
returns the module-owned `astia::net::netdev`, which the loader adds to the
single network registry. Removal unregisters that object before the driver
context is destroyed. This path uses the net registry only and has no
device-file adapter.

RTL8139 currently provides the typed C++ ring/parser, bounded paths, reset and
cancel behavior, and QEMU device-creation smoke. It does not claim a TCP/IP
stack or a packet-delivery integration beyond the net subsystem callbacks.
