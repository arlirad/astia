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
and `sync()` (superblock flush) are implemented through the kernel VFS. All
metadata writes go through `volume::write_block` / `volume::write_bytes`, so a
later journal layer can intercept them without touching the VFS adapter.

A mounted image without a journal is writable. `initialize()` rejects an image
whose journal needs replay (the `INCOMPAT_RECOVER` bit); a clean journal image
mounts read-only because this chunk does not replay journals yet.

Unlink follows POSIX deferred-free semantics: while an open handle still
references the inode, `unlink` clears `links_count`, records `dtime` and keeps
the bitmap bit and data blocks allocated. The last handle close frees the
extents (when the inode uses them) and the inode. The mount keeps a bounded
table of 64 open inodes; opening another inode when it is full fails.

## Limits

- No journal replay and no journal writing.
- No HTree insertion or conversion: existing indexed directories are read and
  can have entries removed, but creation/mutation inside one returns `-ENOSYS`
  until HTree insertion exists, so Linux and `debugfs` never miss an entry
  that Astia's linear scan found.
- No inline data, xattrs, ACLs, or encryption.
- Extent trees only (the extents feature is required); no indirect-block files.
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
- `ext4_qemu_module_load` boots `astiakrn_module_runtime` with `ext4.km` in
  `mod/` and checks the serial log.
