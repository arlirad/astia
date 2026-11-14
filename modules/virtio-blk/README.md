# virtio-blk loadable module

`virtio_blk.cpp` is a bounded x86-64 ET_REL module for modern virtio-blk-pci
devices. It scans the PCI capability chain, maps common, notify, and device
configuration regions, negotiates `VIRTIO_F_VERSION_1` and
`VIRTIO_BLK_F_FLUSH`, and registers `/dev/vblk0` through the generic module
block-device ABI.

The driver uses one split virtqueue backed by contiguous DMA memory and bounce
buffers for reads and writes. Reads and writes are limited to 8192 bytes and
must be 512-byte aligned. Writes are exposed only when the device is not
read-only and offers flush; every successful write is followed by a VirtIO
flush. Completion is bounded polling, with queue reset for timeout and
cancellation when `VIRTIO_F_RING_RESET` is available. Devices without safe
flush support remain read-only and writes return `EROFS`.

`fixture.raw` contains a known `ASTIA_VI...` sector. `run-qemu.sh` builds the
kernel and ET_REL module, packages the module into a newc initrd before PCI
enumeration, boots QEMU with a modern virtio-blk-pci device, and checks the
known-sector, invalid-geometry, write/flush persistence, and read-only
fallback paths. Set `VIRTIO_BLK_READONLY=1` to exercise the fallback.
