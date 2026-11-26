#pragma once

#include <astia/common.hpp>

#include "decompress.hpp"
#include "initrd.hpp"

// One normalized boot initrd shared by the initrd VFS and module discovery.
//
// The bootloader hands over a raw or compressed container.  This module
// validates it once, keeps raw images zero-copy, and owns any decompressed
// backing frames for the rest of the boot.  The VFS mount keeps file spans
// into this view for the kernel's lifetime, so the owner is never released
// after publication.

namespace astia::initrd {
    /// Load and publish the boot initrd.  Idempotent: later calls report the
    /// result of the first attempt.  Raw images stay in place; gzip and zstd
    /// images are decoded into owned, contiguous frames.
    bool load_boot_archive(const void* image, usize length);

    bool boot_archive_ready();

    /// The published archive.  False until `load_boot_archive()` succeeds.
    bool open_boot_archive(archive& out);

    archive_container boot_archive_container();

    /// Bytes the bootloader supplied (compressed or raw).
    usize boot_archive_source_size();

    /// Bytes of normalized cpio consumed by the VFS and module discovery.
    usize boot_archive_stored_size();

    /// Error from the most recent load attempt, if it failed.
    decode_error boot_archive_error();
} // namespace astia::initrd
