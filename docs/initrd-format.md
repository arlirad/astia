# Initrd archive formats

The kernel initrd reader accepts uncompressed cpio `newc` and `crc` archives.
Both are parsed zero-copy from the boot module. New production images use
`newc`; `crc` remains supported for compatibility and format coverage.

The reader recognizes gzip (`1f 8b`) and zstd (`28 b5 2f fd`) outer signatures
for diagnostics, but `open()` rejects them. This is deliberately not advertised
as compression support: the freestanding kernel has no decompressor yet.

The next compression slice must add independently bounded gzip/DEFLATE and
zstd decoders, allocate an owned destination through the frame allocator, cap
expanded bytes before allocation and during output, validate the gzip/zstd
stream checksum, and hand the resulting raw cpio image to the existing reader.
Until then, packagers must pass an uncompressed cpio archive.
