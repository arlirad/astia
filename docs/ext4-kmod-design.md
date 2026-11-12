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

The repository has no block-device subsystem under `kernel/src/dev/blk` and no
stable block-device capability or geometry contract. `fs::open_file` can read a
registered VFS node, but there is no block-device identity contract that a
filesystem mount can safely retain and use for block reads.

The module loader currently loads ET_REL images from boot modules and calls
`module_main`, but the exported-symbol table does not expose filesystem type
registration or a lifetime-safe unregister operation. `fs::register_type` is
therefore intentionally not called by this slice. Adding a module object that
pretends to register ext4 would either make it built-in or leave a dangling
filesystem factory after unload.

The existing mount factory accepts only a source string and has no mount
options or source-open capability contract. A future integration should first
add an exported registration handle with explicit load/unload ownership, a
block-device `read_at` capability, and mount teardown rules. Ext4 must remain
optional and must not replace the root mount automatically.

## Fixture

`kernel/tests/make_ext4_fixture.sh` creates a small journal-free image with
features outside this bounded parser disabled. It requires `mkfs.ext4` and
`debugfs`/`mke2fs` tooling and is intentionally a standalone fixture command;
it is not part of the production kernel build.
