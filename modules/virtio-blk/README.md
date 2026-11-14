# virtio-blk loadable module

`virtio_blk.cpp` is a bounded, read-only x86-64 ET_REL module for modern
virtio-blk-pci devices. It matches the non-transitional PCI ID `1af4:1042`
and block class, scans the PCI capability chain, maps the common, notify, and
device configuration regions, negotiates `VIRTIO_F_VERSION_1`, and registers
`/dev/vblk0` through the generic module block-device ABI.

The driver uses one split virtqueue backed by contiguous page DMA memory and
one DMA bounce buffer per read. Reads are limited to 8192 bytes and must be
512-byte aligned. Completion is bounded polling; queue reset is used for
timeout recovery and teardown when `VIRTIO_F_RING_RESET` is available. No
write callback is registered, so the generic block-device contract rejects
writes with `EROFS`.

`fixture.raw` contains a known `ASTIA_VI...` sector. `run-qemu.sh` builds the
kernel and ET_REL module, packages the module into a newc initrd before PCI
enumeration, boots QEMU with a forced modern virtio-blk-pci device, and checks
the known-sector, invalid-geometry, and read-only paths.
