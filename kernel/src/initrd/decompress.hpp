#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

#include "initrd.hpp"

// Bounded outer-container decoding for initrd images.
//
// `decoded_size()` validates the compressed framing enough to report the
// exact number of bytes the caller must provide.  `decode()` then fills that
// buffer.  Both are single-shot: the caller owns the output storage, so a
// failed decode never publishes partially written data.
//
// The decoder boundary never allocates.  gzip uses a static inflate state and
// zstd uses a static context; the only memory either writes is the output span
// handed in.  Every size is checked against the explicit limits below before
// any output is touched.

namespace astia::initrd {
    enum class decode_error : u8 {
        ok,
        unsupported,
        malformed,
        truncated,
        checksum,
        input_limit,
        output_limit,
        ratio_limit,
        window_limit,
        trailing_data,
        allocation,
    };

    /// Compressed boot input may not exceed this many bytes.
    inline constexpr usize max_compressed_bytes = 32 * 1024 * 1024;

    /// Normalized archive may not exceed this many bytes.
    inline constexpr usize max_decompressed_bytes = 64 * 1024 * 1024;

    /// Maximum expansion ratio applied above `expansion_ratio_floor`.
    inline constexpr usize max_expansion_ratio = 128;

    /// Outputs at or below this size bypass the ratio policy.
    inline constexpr usize expansion_ratio_floor = 1024 * 1024;

    /// Largest zstd window the kernel is willing to replay.
    inline constexpr usize max_zstd_window = 16 * 1024 * 1024;

    struct decode_limits {
        usize max_input   = max_compressed_bytes;
        usize max_output  = max_decompressed_bytes;
        usize max_ratio   = max_expansion_ratio;
        usize ratio_floor = expansion_ratio_floor;
        usize max_window  = max_zstd_window;
    };

    /// Diagnostics for logs and tests.
    const char* decode_error_name(decode_error error);
    const char* container_name(archive_container container);

    /// Report the declared decompressed size of a compressed container.
    /// `container` must be `gzip` or `zstd`; raw archives belong to `open()`.
    decode_error decoded_size(archive_container container, span<const u8> image,
                              const decode_limits& limits, usize& size);

    /// Decode into exactly `output`.  The caller obtains its size from
    /// `decoded_size()`, so a size mismatch is a contract violation.
    decode_error decode(archive_container container, span<const u8> image,
                        span<u8> output, const decode_limits& limits);
} // namespace astia::initrd
