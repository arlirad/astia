# Read-only ext4 module boundary

The parser in `kernel/src/mod/ext4_reader.*` is deliberately independent of
the VFS and is not linked into the kernel's `mod` library. It accepts a
bounded `read_at` callback and covers the on-disk path needed by a first
read-only implementation: superblock and feature validation, group
descriptors, inode records, direct/indirect blocks, extent trees, classic
directory entries, and regular-file reads.

The parser rejects journals, indexed directories, unsupported incompatible or
read-only-compatible features, malformed bounds, and metadata checksum
failures. It does not replay a journal or implement writes, fsck, xattrs,
inline data, encryption, case folding, symlinks, or htree lookup.

## Integration blockers

`kernel/src/dev/blk/block_device.hpp` now provides the minimal kernel-internal
block capability on top of `dev::device`. Requests use the existing
`io_request`/`io_flight`/`io_packet` path; offset and length must both be
multiples of the device block size and invalid requests return `-EINVAL`.
`dev::device_file` is the small VFS adapter. The block implementation is
read-only by default, while geometry and device identity beyond these bounds
remain future work.

Loadable module sources and driver fixtures live under the repository-root
`modules/` tree. The module loader currently loads ET_REL images from boot modules and calls
`module_main`, but the exported-symbol table does not expose filesystem type
registration or a lifetime-safe unregister operation. This slice adds only the
kernel-owned `fs::register_module_filesystem` /
`fs::unregister_module_filesystem` name-and-handle registry. It does not expose
that registry to modules, mount anything, or retain a C++ filesystem object.
The registration handle must be explicitly unregistered before its module is
unloaded. `fs::register_type` is intentionally not called by this slice.

The existing mount factory accepts only a source string and has no mount
options or source-open capability contract. A future integration should first
add a safe module-facing registration ABI with explicit load/unload ownership,
device geometry/identity, and mount teardown rules. Ext4 must remain optional
and must not replace the root mount automatically. The current C++ virtual
`fs::filesystem` / `fs::mount` interfaces are not safe module exports.

## Fixture

`kernel/tests/make_ext4_fixture.sh` creates a small journal-free image with
features outside this bounded parser disabled. It requires `mkfs.ext4` and
`debugfs`/`mke2fs` tooling and is intentionally a standalone fixture command;
it is not part of the production kernel build.
