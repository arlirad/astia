# RTL8139 bounded network foundation

This directory contains a C++-primary RTL8139C(L)+ C-mode foundation. The
module matches Realtek PCI vendor `10ec`, device `8139`, class `02/00`, enables
PCI bus mastering, maps a memory BAR, allocates the 8 KiB plus 16 byte C-mode
receive allocation, performs software reset, configures receive/transmit
registers, and requests MSI-X when the platform exposes it. The existing
module ABI has no generic INTx or network-device registration adapter, so a
QEMU device without MSI-X is reported as `probed_no_irq`; this code does not
claim a kernel network stack or a registered network interface.

`rtl8139_protocol.hpp` owns the typed register offsets, little-endian access,
page-size calculation, C-mode receive-header parsing, and bounded queues. The
receive parser returns one or two borrowed segments because the datasheet
allows a packet to wrap at the 8 KiB ring boundary. The four-byte receive
header and four-byte CRC are not part of the packet view. A view is valid only
during the callback that receives it. An IRQ callback must be nonblocking and
must not allocate, sleep, take a lock, call arbitrary stack code, or retain the
view. Returning `defer` places the borrowed view on the bounded SPSC deferred
path; `rtl8139_poll` invokes the deferred callback in stack/process context and
then recycles the ring space. The deferred queue is bounded to two entries. A full deferred queue drops and recycles the
packet. Code needing a longer lifetime must copy the segments in its process
context.

TX submission is a bounded lock-free MPSC queue. A successful submission
transfers ownership of the DMA-safe buffer until its reclaim callback runs.
`rtl8139_service_tx` is process context and assigns queued requests to the four
C-mode descriptors. The IRQ handler only acknowledges status, harvests
completed descriptors, and places completion records on a bounded SPSC queue;
it never calls reclaim callbacks. `rtl8139_poll` runs reclaim callbacks in
process context. A full queue returns `-11` without taking ownership. Reset
and detach are process-context operations: they mask and disable interrupts,
cancel queued and outstanding requests with `-125`, and release DMA resources
only after all borrowed state has been recycled.

The implementation follows RTL8139CL+ datasheet sections 6.1, 6.2, 6.5--6.9,
8.1--8.5, and 9.1--9.2. C-mode uses the packet header status/length, four
double-word-aligned transmit addresses, CAPR/CBR ring pointers, WRAP=0 and
the 8 KiB plus 16 byte allocation. C+ descriptor mode and checksum/VLAN/LRO
offloads are intentionally not enabled.

Build the ET_REL object and host protocol tests with CMake. The QEMU target
only verifies that `-device rtl8139` can be created with a user-mode network
backend; it does not claim RX, TX, PCI interrupt delivery, or a network stack.
