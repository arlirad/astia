# ext4 module

A loadable ext4 filesystem for Astia. The module owns the on-disk layer
(`ext4_reader.*`, `ext4_disk.*`, `ext4_space.*`, `ext4_extent.*`,
`ext4_dir.*`) and the VFS adapter (`ext4.hpp`, `ext4_mount.cpp`,
`ext4_file.cpp`), and registers one `astia::ext4::filesystem` factory with the
kernel filesystem registry from `module_main.cpp`.

## Contract

- `module_main()` calls `astia::ext4::register_filesystem()`, which registers a
  static `fs::filesystem` instance through `fs::register_type()`. It logs
  `ext4: ready` on success and returns `enone`.
- The filesystem factory accepts a block-device source path. `initialize()`
  opens it read-write, retrying read-only, and rejects images that are not
  ext4, lack the extents feature, or whose block count exceeds the device.
- Unloading calls `astia::ext4::unregister_filesystem()` (from a destructor),
  which refuses to unregister while any mount or in-flight registry operation
  still references the type.

## Read-write operations

Open/create/truncate, read/write, mkdir, rmdir, unlink, rename, hard link,
symlink/readlink, chmod, chown, touch, directory enumeration, stat metadata,
and `sync()` are implemented through the kernel VFS. All metadata writes go
through `volume::write_block` / `volume::write_bytes`, which stage them in the
JBD2 journal while a transaction is active.

Read-write mounts are journaled. Every VFS mutation ends one transaction
(descriptor, staged metadata, commit) and checkpoints it, including the
superblock free counts. File data blocks are written directly to their home
blocks before the metadata transaction commits (data=ordered). A mounted image
without a journal stays writable through the direct block path.

Transactions are published to the journal superblock before their home blocks
are checkpointed; file data is written before the metadata commit and there is
no explicit cache-flush primitive, so the module assumes durable synchronous
writes, and a single operation may span multiple transactions if it exceeds the
48-tag staging bound (partial progress is preserved).

`initialize()` opens the journal, replays it when `needs_recovery`
(`INCOMPAT_RECOVER` / a nonzero journal `s_start`) is set, then enables
`CSUM_V3` on journals created without checksum features before attaching the
journal to the volume. A journal that cannot be opened or upgraded makes the
mount read-only (or fails when replay is required).

Before a read-write mount is usable, `initialize()` processes the old-style
orphan list (`s_last_orphan`, which overloads `i_dtime` as the next inode
number) and, when `RO_COMPAT_ORPHAN_PRESENT` is set, the orphan file
(`s_orphan_file_inum`). Unlinked orphans are freed (extents, then the inode);
inodes still linked are treated as interrupted truncates and have `dtime`
cleared. Entries are rewritten with a fresh `ob_checksum`, `orphan_present` is
cleared, the superblock is marked clean, and the whole cleanup commits through
the attached journal. A cleanup failure refuses the mount; a read-only source
mounts read-only and logs that orphans were not processed.

Unlink follows POSIX deferred-free semantics: while an open handle still
references the inode, `unlink` registers the inode in the orphan file (or the
legacy `s_last_orphan` list when the orphan-file feature is absent), clears
`links_count`, records `dtime` and keeps the bitmap bit and data blocks
allocated. If the kernel crashes before the last close, the next read-write
mount replays the journal and frees the registered orphan. The last handle
close removes the orphan entry, frees the extents (when the inode uses them)
and the inode, and commits. The mount keeps a bounded table of 64 open inodes;
opening another inode when it is full fails.

## Limits

- No HTree insertion: the first mutation of an existing indexed directory
  converts it to a linear directory, preserving every entry, and continues with
  the linear path. The hash index is not rebuilt; `e2fsck -D` can re-index it.
- No inline data, xattrs, ACLs, or encryption.
- Extent trees only (the extents feature is required); no indirect-block files.
- Stock `mke2fs` layouts mount read-write: `flex_bg` allocation, 64-byte group
  descriptors with the `64bit` feature, and `metadata_csum_seed` are supported.
  `RO_COMPAT_ORPHAN_PRESENT` is accepted and its pending orphans are cleaned up
  before the mount is usable.
- The mount lock assumes a synchronous block device: the module's device path
  is task context and `dv_sync`, so a spinlock is sufficient. An asynchronous
  device would need a sleeping lock and a different I/O submission path.

## Tests

- `ext4_unit` builds `tests/ext4_test.cpp` against the reader on the host.
- `astia_ext4_real_test` reads a checked mkfs.ext4 image on the host.
- `astia_ext4_vfs_test` registers this module with the kernel VFS and exercises
  mount, lookup, readdir, symlink, and journal/read-only handling.
- `astia_ext4_rw_vfs_test` mounts a real mkfs.ext4 image through the kernel VFS,
  exercises creation, writes, rename, link, symlink, chmod, truncate and
  removal, then remounts. The CTest entry runs `e2fsck -fn` and `debugfs`
  assertions on the resulting image.
- `ext4_journal_vfs_test` runs the same tool against a journaled fixture in
  `normal`, `crash` (power loss after commit, before checkpoint) and `recover`
  (real VFS replay on mount) modes, then checks both images with `e2fsck -fn`,
  `dumpe2fs` and `debugfs`, including the replayed 8 KiB file contents.
- `ext4_stock_test` mounts stock `mke2fs` images (1 KiB and 4 KiB blocks,
  default features) through the same tool in `normal` mode and checks the
  output with `e2fsck -fn`, `dumpe2fs` and `debugfs`.
- `ext4_orphan_test` builds an unlinked `/victim` fixture, plants it on the old
  orphan list and in the orphan file, then mounts read-write through the same
  tool and checks with `e2fsck -fn` and `dumpe2fs` that exactly one inode was
  freed and `orphan_present` is cleared.
- `ext4_indexed_test` builds an `e2fsck -D` HTree-indexed `/big` directory,
  inserts two files through the same tool in `indexed` mode, and checks with
  `e2fsck -fn` and `debugfs` that every entry survived the one-time conversion
  and that the index flag is cleared.
- `ext4_qemu_module_load` boots `astiakrn_module_runtime` with `ext4.km` in
  `mod/` and checks the serial log.
