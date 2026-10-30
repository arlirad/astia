#pragma once

#include <astia/common.hpp>

// A zero-copy reader for ustar (POSIX) tar archives, used to unpack an initrd.
//
// The archive stays exactly where the bootloader put it; callers hand
// open() a view of that memory (the direct-map view for boot modules),
// and files are handed out as pointers into that image rather than
// copied out, so opening an archive costs nothing and a file's bytes can
// be fed straight to the ELF loader.
//
// Scope: ustar headers with regular files. Directory and special entries
// (links, devices, ...) are skipped rather than surfaced, and the long-name
// `prefix` field is ignored — a simple initrd keeps names under 100 chars.

namespace astia::initrd {

    /// Size of a tar header / the archive block size.
    inline constexpr usize block_size = 512;

    /// Fixed-width fields and protocol marker sizes in a ustar header.
    inline constexpr usize name_field_size  = 100;
    inline constexpr usize size_field_size  = 12;
    inline constexpr usize ustar_magic_size = 5;
    inline constexpr char  ustar_magic[ustar_magic_size] = {'u', 's', 't', 'a', 'r'};

    /// One regular file in the archive. `name` and `data` point into the
    /// archive image and stay valid for as long as it does.
    struct file {
        const char* name = nullptr;
        const void* data = nullptr;
        usize       size = 0;
    };

    /// An opened archive: a validated view over a tar image held in place.
    struct archive {
        const u8* base = nullptr;
        usize     len  = 0;
    };

    /// Visit callback for each(). Return false to stop enumeration early.
    using visit_fn = bool (*)(void* ctx, const file& f);

    /// Open a tar image. Fails on anything too short to be an archive, or
    /// whose first block is neither a ustar header nor the end-of-archive
    /// marker.
    bool open(const void* image, usize len, archive& out);

    /// Visit each regular file (non-empty) in the archive, in order. Stops
    /// early — returning false — if `visit` returns false. Returns true only
    /// if it walked the whole archive without hitting a malformed header.
    bool each(const archive& a, visit_fn visit, void* ctx);

    /// Find the first regular file named `name`. Returns false if absent.
    bool find(const archive& a, const char* name, file& out);

    /// Number of regular (non-empty) files in the archive.
    usize count(const archive& a);

} // namespace astia::initrd
