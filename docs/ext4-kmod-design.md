# Read-only ext4 module boundary

The parser in `kernel/src/mod/ext4_reader.*` is deliberately independent of
the VFS and is not linked into the kernel's `mod` library. It accepts a
bounded `read_at` callback and covers the on-disk path needed by a first
read-only implementation: superblock and feature validation, group
descriptors, inode records, direct/indirect blocks, extent trees, classic and
indexed directory entries, and regular-file reads.

The parser rejects journals, unsupported incompatible or read-only-compatible
features, malformed bounds, and metadata checksum failures. Indexed directory
enumeration validates HTree metadata and walks its leaf blocks; it does not
perform hash-directed lookup. It does not replay a journal or implement
writes, fsck, xattrs, inline data, encryption, case folding, symlinks, or
other HTree operations.

## Integration blockers

`kernel/include/astia/dev/blk/blkdev.hpp` provides the minimal public
block-driver contract on top of `dev::blk::blkdev`, built on `dev::device`.
Requests use the existing
`io_request`/`io_flight`/`io_packet` path; offset and length must both be
multiples of the device block size and invalid requests return `-EINVAL`.
`dev::device_file` is the small VFS adapter. The block implementation is
read-only by default, while geometry and device identity beyond these bounds
remain future work.

Loadable module sources and driver fixtures live under the repository-root
`modules/` tree. The module loader packages and loads ET_REL images from boot
modules and calls `module_main`; packaging is not a separate filesystem kind.
Every filesystem is an ordinary first-class `fs::filesystem` registered through
the filesystem registry. A type must be unregistered before its implementation
is unloaded, and unregister is rejected while factories or mounts still refer to
it. The C++ filesystem interfaces are public under
`kernel/include/astia/fs/`: `filesystem.hpp`, `mount.hpp` and the
module-facing `module.hpp`.

## Implemented module boundary

`modules/ext4/` is a loadable module. Its `module_main()` registers a static
`fs::filesystem` factory with `fs::register_type()`; a destructor unregisters it
before unload, and the registry refuses unregister while mounts or in-flight
operations still reference the type. Boot-loaded modules depend on the `fs`
init step, so `register_type()` runs against a ready registry. `fs::init()`
registers only the built-in devfs, initrd and tmpfs types.

The existing mount factory accepts only a source string and has no mount
options or source-open capability contract. Ext4 remains optional and does not
replace the root mount automatically. Write support, journal replay and
mutation are the next step; this slice stays read-only.

## Fixture

`kernel/tests/make_ext4_fixture.sh` creates a small journal-free image with
features outside this bounded parser disabled. It requires `mkfs.ext4` and
`debugfs`/`mke2fs` tooling and is intentionally a standalone fixture command;
it is not part of the production kernel build.
