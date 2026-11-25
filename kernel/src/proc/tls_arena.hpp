#pragma once

#include <astia/mm/phys_region.hpp>
#include <astia/span.hpp>

namespace astia::proc {

    /// The DSO static-TLS arena is address space first, memory on demand.
    /// The payload block and its TCB headroom keep their fixed position at
    /// the top; the reserve below them is a lazy region whose pages
    /// materialise on first touch, so an interpreter that lays out only a
    /// few hundred bytes of DSO TLS does not pay for the whole 64 KiB limit.
    struct tls_arena {
        u64   reserve_bytes  = 0;
        usize reserve_pages  = 0;
        usize payload_pages  = 0;
        usize total_pages    = 0;
        u64   payload_offset = 0;
    };

    /// Size the arena for `payload_pages` eagerly materialised frames.
    /// False when the geometry would overflow or has no payload.
    bool measure_tls_arena(usize payload_pages, tls_arena& out);

    /// Create the region `measure_tls_arena` sized: the payload pages are
    /// allocated zeroed, the reserve pages stay unmaterialised.  nullptr on
    /// allocation failure.
    mm::phys_region* create_tls_arena(const tls_arena& layout);

    /// Copy `source` into the payload at `offset` (relative to the payload's
    /// first byte).  Lazy pages live in independent frames, so the copy is
    /// page-granular rather than one contiguous run.
    bool write_tls_payload(mm::phys_region& region, const tls_arena& layout, u64 offset,
                           span<const u8> source);

    bool write_tls_word(mm::phys_region& region, const tls_arena& layout, u64 offset,
                        u64 value);

} // namespace astia::proc
