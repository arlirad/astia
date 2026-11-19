#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

// A zero-copy reader for uncompressed newc/cpio-crc archives, used to unpack
// an initrd.
//
// The archive stays exactly where the bootloader put it; callers hand
// open() a view of that memory (the direct-map view for boot modules),
// and files are handed out as pointers into that image rather than
// copied out, so opening an archive costs nothing and a file's bytes can
// be fed straight to the ELF loader.
//
// Scope: regular files with non-empty data. Directory and special entries
// (links, devices, ...) are skipped rather than surfaced.

namespace astia::initrd {

    enum class archive_format : u8 {
        cpio_newc,
        cpio_crc,
    };

    /// Outer compression signatures recognized for diagnostics. `open()` only
    /// accepts raw archives; gzip and zstd are intentionally rejected until a
    /// bounded kernel decompressor exists.
    enum class archive_container : u8 {
        raw,
        gzip,
        zstd,
    };

    /// One regular file in the archive. `name` and `data` point into the
    /// archive image and stay valid for as long as it does.
    struct file {
        const char* name = nullptr;
        const void* data = nullptr;
        usize       size = 0;
    };

    /// An opened archive: a validated view over a cpio image held in place.
    struct archive {
        span<const u8> image;
        // Compatibility views for existing in-kernel consumers.
        const u8*      base   = nullptr;
        usize          len    = 0;
        archive_format format = archive_format::cpio_newc;
    };

    /// Visit callback for each(). Return false to stop enumeration early.
    using visit_fn = bool (*)(void* ctx, const file& f);

    /// Identify an outer container without attempting decompression.
    archive_container        detect_container(span<const u8> image);
    inline archive_container detect_container(const void* image, usize length) {
        return detect_container(span<const u8>(static_cast<const u8*>(image), length));
    }

    /// Open a newc or crc image. Gzip and zstd images are rejected; detection
    /// is diagnostic only and does not provide decompression.
    bool        open(span<const u8> image, archive& out);
    inline bool open(const void* image, usize length, archive& out) {
        return open(span<const u8>(static_cast<const u8*>(image), length), out);
    }

    /// Visit each regular file (non-empty) in the archive, in order. Stops
    /// early — returning false — if `visit` returns false. Returns true only
    /// if it walked the whole archive without hitting a malformed header.
    bool each(const archive& a, visit_fn visit, void* ctx);

    /// Find the first regular file named `name`. Returns false if absent.
    bool find(const archive& a, const char* name, file& out);

    /// Number of regular (non-empty) files in the archive.
    usize count(const archive& a);

} // namespace astia::initrd
