# Initrd archive formats

The boot initrd is an outer container around a cpio archive. The kernel
accepts:

- raw cpio `newc` (`070701`), parsed zero-copy from the boot module;
- raw cpio `crc` (`070702`), also parsed zero-copy and checksum-validated;
- a gzip (`1f 8b`) member containing a `newc` or `crc` archive;
- a zstd (`28 b5 2f fd`) frame containing a `newc` or `crc` archive.

New production images use `newc`. `crc` remains supported for compatibility.
Gzip and zstd are outer-container compressions only: files inside the archive
are not individually compressed by this layer.

## One normalized boot archive

`kernel/src/fs/initrd_init.cpp` loads the boot module through
`astia::initrd::load_boot_archive()` before mounting `/`.
## Module layout

ET_REL modules live under `mod/` in the archive (`mod/ip.km`,
`mod/rtl8139.km`, ...).  `kernel/src/mod/init.cpp` loads only relocatable files
from that directory; every other entry stays an ordinary initrdfs file.
Staging scripts and the QEMU fixtures follow the same convention.

`kernel/src/mod/init.cpp` opens the same published archive for ET_REL module
discovery instead of opening or decompressing the image a second time.

Raw images remain in place: file spans point into the bootloader's module.
Compressed images are decoded once into contiguous frames owned by the initrd
subsystem. The VFS mount and any open files retain spans into that storage for
the kernel's lifetime, so the owner is never released after publication. A
failed load releases its temporary frames and publishes nothing; in
particular, no root mount and no module execution happens from a partially
decoded archive.

## Decoder boundary

`kernel/src/initrd/decompress.{hpp,cpp}` is a single-shot API:

1. `decoded_size()` validates framing and returns the exact output size.
2. `decode()` fills a caller-owned buffer of exactly that size.

The decoder never allocates. gzip uses a static `tinfl` state; zstd uses a
static context and an exact-size output buffer. Both decoders live in the
vendored `third_party/` trees described in `third_party/README.md`; the zstd
allocation hooks fail closed. Decompression is single-threaded and only runs
during init.

The normalized archive is fully validated (`initrd::validate()`) before it is
published. The cpio parser stays authoritative after decompression: malformed
headers, names, alignment, file bounds, checksums, missing trailers, and
unsupported file types behave exactly as they do for raw input.

## Accepted subset

- gzip: one member, DEFLATE, optional `FEXTRA`/`FNAME`/`FCOMMENT`/`FHCRC`,
  trailer CRC32 and ISIZE validated. Concatenated members and trailing bytes
  are rejected.
- zstd: one standard frame without a dictionary, with a declared frame content
  size, valid block boundaries, and the frame checksum when present. Skippable
  frames, frames without a content size, and trailing bytes are rejected.

## Limits

Defined in `kernel/src/initrd/decompress.hpp`:

| Limit | Value |
| --- | --- |
| Compressed input | 32 MiB |
| Decompressed archive | 64 MiB |
| Expansion ratio above 1 MiB | 128:1 |
| zstd window | 16 MiB |

Every size is checked before allocation. Declared output sizes larger than the
limit fail before any frame is allocated; the ratio policy rejects a tiny
compressed input that claims a large archive.

## Diagnostics

`decode_error_name()` is the stable log taxonomy: `unsupported`, `malformed`,
`truncated`, `checksum mismatch`, `compressed input limit`,
`decompressed output limit`, `expansion ratio limit`, `decoder window limit`,
`trailing data`, and `decoder allocation`. The boot log reports the selected
container and the compressed/normalized byte counts, never raw addresses.

Host fixtures are generated with `gzip` and `zstd`; those host tools are test
and packaging dependencies only, never kernel runtime dependencies.
