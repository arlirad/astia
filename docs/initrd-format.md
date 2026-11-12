# Initrd archive formats

The kernel initrd reader currently accepts uncompressed ustar, cpio `newc`,
and cpio `crc` archives. All three are parsed zero-copy from the boot module,
and existing ustar images remain supported.

The reader recognizes gzip (`1f 8b`) and zstd (`28 b5 2f fd`) outer signatures
for diagnostics, but `open()` rejects them. This is deliberately not advertised
as compression support: the freestanding kernel has no decompressor yet.

The next compression slice must add independently bounded gzip/DEFLATE and
zstd decoders, allocate an owned destination through the frame allocator, cap
expanded bytes before allocation and during output, validate the gzip/zstd
stream checksum, and hand the resulting raw cpio image to the existing reader.
Each decoder needs malformed, truncated, checksum, and expansion-limit tests,
followed by a QEMU boot test using compressed initrds. Until then, packagers
must pass an uncompressed archive.
