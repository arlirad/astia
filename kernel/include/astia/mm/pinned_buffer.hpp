#pragma once

#include <astia/common.hpp>
#include <astia/mm/phys_region.hpp>

// A user range held still for the duration of an asynchronous operation.
//
// Pinning is a snapshot, not a lockout.  At submission the range is resolved
// to one backing region and referenced; after that the mapping may be
// unmapped, re-mapped or CoW-split freely, because the operation no longer
// touches the user pointer at all — it holds the region.  That is what
// closes the TOCTOU the syscall boundary warns about ("it is TOCTOU against a
// concurrent munmap from another thread of the same process; blocking
// syscalls must therefore keep no user pointer across the block"), and what
// lets a device move bytes or program DMA without racing the page tables:
// the addresses it is given stay mapped and stay backed until unpin().
//
// v1 pins a range inside a *single* phys_region.  That covers the case that
// matters without needing scatter/gather: a region is a contiguous frame run
// and the kernel direct map is linear, so the whole pinned range is one
// contiguous kernel window — direct() — which a device can be handed instead
// of the user's address.  The window is page-granular, like the pin itself:
// direct() names the first pinned page, and a range that started mid-page is
// reached by adding that offset, which the operation's own description of
// the buffer — not the pin — carries.
//
// The pin holds two references on the region, and it needs both.  The
// mapping coverage keeps every frame out of the allocator; the allocation
// reference keeps the per-page counters themselves alive, so unpin() can
// still decrement them if the mapping — and with it the last ordinary
// reference — is gone by then.  release_range() drops exactly that pair;
// take only the coverage and a concurrent unmap frees the region out from
// under the pin, leaving it decrementing freed memory.

namespace astia::mm {

    class pinned_buffer {
        public:
        pinned_buffer()                                = default;
        pinned_buffer(const pinned_buffer&)            = delete;
        pinned_buffer& operator=(const pinned_buffer&) = delete;

        /// A pin changes owner (a park slot hands it to the reaper) rather
        /// than being duplicated: reference-counted state must never have
        /// two owners.  Move-assignment releases whatever the target
        /// already held, which also makes it the "drop any stale pin" step
        /// when a slot is recycled.
        pinned_buffer(pinned_buffer&& other) noexcept;
        pinned_buffer& operator=(pinned_buffer&& other) noexcept;

        /// Reference [first, first + count) of `region` for the caller.
        /// `destination` is true when the operation will *write* into the
        /// buffer and the caller will read the bytes back through this
        /// mapping (a read from the object): the frames are then marked so
        /// a CoW split cannot move the mapping out from under the
        /// operation.  False — referencing nothing — when the range escapes
        /// the region or a counter would overflow.
        bool acquire(phys_region* region, usize first, usize count, bool destination);

        /// Drop the references taken by acquire().  Safe and idempotent on
        /// an empty buffer.  Task context only: it may take a lock and may
        /// free the region, so it must never run from an interrupt handler.
        void release();

        bool valid() const {
            return _region != nullptr;
        }
        bool  contiguous() const;
        usize pages() const {
            return _count;
        }

        /// Kernel address of page `index` — what a device should be handed.
        void* direct(usize index = 0) const;

        /// Physical address of page `index`, for a device that programs its
        /// own DMA.
        u64 phys(usize index = 0) const;

        private:
        phys_region* _region      = nullptr;
        usize        _first       = 0;
        usize        _count       = 0;
        bool         _destination = false;
    };

} // namespace astia::mm
