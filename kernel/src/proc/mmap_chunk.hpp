#pragma once

#include <astia/common.hpp>

namespace astia::proc {
    /// Upper bound on the pages munmap/mprotect touch while holding
    /// `_mmap_lock`.  A call may cover 1 << 20 pages; keeping each hold to
    /// one batch bounds the time interrupts stay masked.
    inline constexpr u64 mmap_chunk_pages = 64;

    /// An ascending, page-aligned batch of a larger page range.
    struct mmap_chunk {
        u64 first = 0; ///< absolute page index
        u64 pages = 0;

        u64 end() const {
            return first + pages;
        }
    };

    /// Number of batches a `pages`-page range is split into.
    constexpr u64 mmap_chunk_count(u64 pages) {
        return (pages + mmap_chunk_pages - 1) / mmap_chunk_pages;
    }

    /// Invoke `fn(mmap_chunk)` for each batch of [first_page, first_page +
    /// pages), in ascending order.  Returns the batch count.
    template <typename Fn>
    u64 for_each_mmap_chunk(u64 first_page, u64 pages, Fn&& fn) {
        u64 count = 0;

        for (u64 done = 0; done < pages;) {
            u64 batch = pages - done;
            if (batch > mmap_chunk_pages)
                batch = mmap_chunk_pages;

            fn(mmap_chunk{first_page + done, batch});

            done += batch;

            ++count;
        }

        return count;
    }
} // namespace astia::proc
