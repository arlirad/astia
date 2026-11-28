# ext4 module design

## Boundary

ext4 is a loadable ET_REL module under `modules/ext4/`, not part of the kernel
image. `modules/ext4/CMakeLists.txt` compiles each source with the kernel's C++
ABI: `-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-mgeneral-regs-only -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1`, together with
`-mcmodel=large` and `-mno-red-zone`, then links the objects with `ld -r` into
`ext4.km`. The loader takes it from `mod/ext4.km`. The `mod` init step depends
on `boot_info`, `interrupts` and `fs`, so boot modules are loaded after the
filesystem registry is ready.

`module_main()` calls `register_filesystem()`, which registers one static
`fs::filesystem` factory through the public `astia/fs/module.hpp` contract using
`fs::register_type()`. The factory reports the name `ext4`, constructs an
`ext4::mount` for a source path, and owns and destroys its mounts. A
`[[gnu::destructor]]` calls `unregister_filesystem()` before unload. If
`fs::unregister_type()` fails with `-EBUSY` because a mount or an in-flight
registry operation still references the type, the module logs the failure and
calls `astia::module::make_resident()`, so the loader cannot unmap code or
vtables that a live mount still points at.

## Layers

- `ext4_reader` is the original read-only parser: bounded callback I/O,
  superblock and feature validation, group descriptors, inodes, extent trees,
  classic and indexed directory entries, and regular-file reads. The mount does
  not use it; it is retained for pure host unit tests.
- `volume` (`ext4_disk.*`) owns superblock and group-descriptor parsing, inode
  records, sector/block I/O, the metadata_csum seed and verification, and the
  journal hooks. It is the only path the mount uses to reach the device.
- `space` allocates and frees blocks and inodes through the group bitmaps and
  keeps the descriptor and superblock counters in step.
- `extent` maps logical blocks through the extent B-tree, allocates and grows
  trees, splits leaf and index nodes, and truncates or frees extents.
- `directory` iterates and searches dirents, creates and removes entries,
  maintains `.` and `..`, spills entries into new blocks, and writes
  checksum tails on metadata_csum directories.
- `journal` implements JBD2 open, replay, checksum upgrade, transactions, and
  checkpointing.
- `ext4_mount` and `ext4_file` are the VFS adapter: open/create, read/write,
  mkdir/rmdir, unlink, rename, link, symlink/readlink, chmod/chown/touch,
  truncate, directory enumeration, stat metadata, and sync, with POSIX
  deferred-free semantics and a bounded table of 64 open inodes.

## On-disk guarantees

Files and directories are extent-mapped; the extents feature is required and
indirect-block files are not supported. Block sizes of 1, 2 and 4 KiB are
accepted; inode sizes 128 through 512; descriptor sizes 32 or 64; revision
level must be 1 and `first_data_block` must match the block size. With
metadata_csum, checksum type 1 (CRC32C) is required.

The metadata_csum path verifies and writes the superblock checksum, group
descriptor checksums, block and inode bitmap checksums, inode checksums, extent
node checksums, and the 12-byte directory tail (or `dx_tail` for indexed
directories). Superblock free counters and state are written with each
committed transaction. File data is written to its home block
before the metadata transaction commits, giving ordered-data semantics.

## Journaling

Read-write mounts use the in-filesystem JBD2 journal. The journal must have a
v2 superblock; v1 journals are refused. CRC32C checksums are supported for v2
and v3 tags, commit blocks and descriptor tails. Replay parses revoke blocks,
honours the `ESCAPE` flag for metadata blocks that begin with the journal magic,
and tracks sequence numbers and ring wrap. A journal with fast commit, unknown
incompatible bits, or any compat/ro-compat bits is unsupported.

Each VFS mutation stages its metadata in one transaction that is checkpointed
at `finish()`; the journal superblock is published before the home blocks are
checkpointed, so a power loss during checkpoint leaves a replayable
transaction. A transaction begins by setting the filesystem `needs_recovery`
flag (`INCOMPAT_RECOVER`) and writing the superblock; a successful commit
checkpoints the staged blocks, clears the flag and sets the superblock state to
clean. File data is written before the metadata commit, and there is no
explicit cache-flush primitive, so the module assumes durable synchronous
writes; a single operation may span multiple transactions if it exceeds the
48-tag staging bound, and partial progress is preserved.
Replay runs before the mount is usable. A journal without checksum features is
upgraded to `CSUM_V3` on the first read-write mount; if that upgrade fails, the
mount falls back to read-only. An unsupported journal also mounts read-only,
but a dirty journal on a read-only source refuses to mount. Images without a
journal remain writable through the direct block path.

## Supported and refused features

`volume::open()` accepts the incompatible features `filetype`, `recover`,
`extents`, `64bit`, `csum_seed` and `largedir`, and the read-only-compatible
features `sparse_super`, `large_file`, `huge_file`, `gdt_csum`, `dir_nlink`,
`extra_isize` and `metadata_csum`. `csum_seed` requires `metadata_csum`, as
does `gdt_csum`; `metadata_csum` requires checksum type 1. The `64bit` bit is
accepted only while the block count still fits in 32 bits.

Everything else is refused at open: inline data, bigalloc, flex_bg,
encryption, quota, the orphan file, external journal devices, unknown feature
bits, and the read-only flag. The mount additionally rejects images without
extents, with blocks beyond the source device, or with a non-directory root.

HTree directories are read, enumerated and can have entries removed, but
creating an entry inside an existing indexed directory returns `-ENOSYS`:
a linear append cannot update the hash index. No xattr, ACL, inline-data,
encryption, or orphan-list cleanup path exists.

## Concurrency and lifetime

Each mount has one `core::spinlock`, held across synchronous device I/O. The
module assumes a synchronous block device (`dv_sync`) whose callbacks run in
task context; an asynchronous device would need a sleeping lock and a different
submission path. Mounts and open files pin the type and module through the
registry's existing active-operation and mount counters, which is what makes
unregister return `-EBUSY` while they are live. The deferred-free table holds
at most 64 open inodes; opening another inode when it is full fails.

## Testing

- `astia_ext4_test` and `ext4_unit` build the reader/disk parser test through
  the kernel test harness and the module's standalone host harness.
- `astia_ext4_real_test` reads a checked `mkfs.ext4` image
  (`ext4-metadata-csum.img`) through the parser.
- `ext4_disk_test` runs the volume layer against an `mke2fs` fixture.
- `ext4_extent_test` exercises extent mapping, growth, splitting and truncation
  against a real image.
- `ext4_dir_test` exercises directory iteration, insertion, removal and
  checksum tails.
- `ext4_journal_test` exercises journal open, transaction staging, commit and
  replay.
- `astia_ext4_vfs_test` registers the module with the kernel VFS and covers
  mount, lookup, readdir, symlink, and journal/read-only handling.
- `astia_ext4_rw_vfs_test` mounts a real `mkfs.ext4` image through the VFS,
  performs create, write, rename, link, symlink, chmod, truncate and removal,
  then remounts. Its CTest entry runs `e2fsck -fn` and `debugfs` assertions on
  the result.
- `ext4_journal_vfs_test` runs the same tool on a journaled fixture in
  `normal`, `crash` (power loss after commit, before checkpoint) and `recover`
  modes, then checks both images with `e2fsck -fn`, `dumpe2fs` and `debugfs`,
  including the replayed 8 KiB file contents.
- `ext4_qemu_module_load` boots `astiakrn_module_runtime` with `ext4.km` in
  `mod/` and checks the serial log for a successful load.

The real-image tests are gated on `mkfs.ext4`, `e2fsck` and `debugfs` being
present. The crash mode deliberately leaves `needs_recovery` set, then proves
that a real VFS replay recovers the committed file contents.

## Known gaps

- HTree insertion and conversion.
- xattrs, ACLs, encryption, and inline data.
- bigalloc and flex_bg.
- 64-bit high block numbers and volumes larger than 16 TiB.
- External journal devices.
- The orphan inode list and orphan-file cleanup.
- Quota.
- fsck-time-only fields such as MMP.
- Parallel journal transactions; the journal commits one transaction at a time.
- A sleeping lock and asynchronous I/O path for non-`dv_sync` devices.
