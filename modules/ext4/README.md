# ext4 module

A loadable, read-only ext4 filesystem for Astia. The module owns the on-disk
parser (`ext4_reader.*`) and the VFS adapter (`ext4.hpp`, `ext4_mount.cpp`,
`ext4_file.cpp`), and registers one `astia::ext4::filesystem` factory with the
kernel filesystem registry from `module_main.cpp`.

## Contract

- `module_main()` calls `astia::ext4::register_filesystem()`, which registers a
  static `fs::filesystem` instance through `fs::register_type()`. It logs
  `ext4: ready` on success and returns `enone`.
- The filesystem factory accepts only a block-device source path. `mount()`
  reads the superblock and root inode and fails without disturbing the caller.
- Unloading calls `astia::ext4::unregister_filesystem()` (from a destructor),
  which refuses to unregister while any mount or in-flight registry operation
  still references the type.
- Boot-loaded modules run after the `fs` init step, so the registry exists by
  the time `module_main()` runs.

## Limits

The module is read-only: writes, truncate, sync, metadata changes, symlink
creation, rename, unlink, mkdir and link all return `-EROFS`. Journals are
rejected rather than replayed. Journaling, write support and filesystem
mutation are the next step.

## Tests

- `ext4_unit` builds `tests/ext4_test.cpp` against the reader on the host.
- `astia_ext4_real_test` reads a checked mkfs.ext4 image on the host.
- `astia_ext4_vfs_test` registers this module with the kernel VFS and exercises
  mount, lookup, readdir, symlink, and read-only rejection.
- `ext4_qemu_module_load` boots `astiakrn_module_runtime` with `ext4.km` in
  `mod/` and checks the serial log.
