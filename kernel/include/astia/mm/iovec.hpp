#pragma once

#include <astia/common.hpp>
#include <astia/mm/pinned_buffer.hpp>

// A scatter/gather edition of the pin: an ordered list of user ranges, all
// held still for the duration of one asynchronous operation.
//
// pinned_buffer answers one question — where are the frames behind this range,
// and will they still be the frames the caller reads when the device gets to
// them.  A readv() or writev() asks it once per entry, and the entries have
// nothing to do with each other: different mappings, different backing
// regions, split at different points.  Resolving them independently is not a
// convenience, it is the whole point: the "one contiguous kernel window" a
// device is handed only holds *within* an entry, so the operation is a walk
// over a list of entries and never a single window.
//
// The walk is a cursor — (index, offset, remaining) — and the cursor is the
// state that has to survive a park.  aretev's iovecman keeps exactly that
// triple and it is worth copying; where it keeps the *list* is what is worth
// not copying.  iovecman borrows the user's iovec array and copies through a
// device-owned bounce buffer, which is correct only while the transfer
// finishes inside the call.  The moment an operation parks — and ours park by
// design, resuming from an interrupt with the submitting stack long gone —
// that borrowed pointer may name unmapped memory.  Here the entries are
// resolved to pinned frames at submission, so nothing the cursor holds is a
// user pointer and the whole structure is safe to resume from an interrupt
// handler.
//
// Not every entry is a user range.  An in-kernel caller has a buffer that
// cannot move, so there is nothing to snapshot — but the operation still walks
// it as an entry (append_kernel), because a caller that parks must not need a
// second engine path just for being kernel memory.
//
// Zero-length entries are legal in the interface and are dropped on the way
// in.  Recording one would give the cursor a place it can stop, and a walk
// that stops on an empty entry reports a short operation that never happened
// — the defect that turns a legal writev() with a hole in it into a truncated
// write.
//
// The capacity is fixed because the list lives in a park slot, and a slot is
// taken under an interrupt guard, where allocating is not an option.  It is a
// v1 bound: an operation with more entries is refused up front, never quietly
// truncated.

namespace astia::mm {

    /// One entry: the frames behind a user range, where that range's bytes
    /// begin inside them, and how many there are.  `frames` stays empty for an
    /// entry that is plain kernel memory, which is what `base` is for.
    struct pinned_segment {
        pinned_buffer frames;
        u8*           base   = nullptr; // kernel address of byte 0, kernel entries
        usize         offset = 0;       // first byte's position inside page 0
        usize         length = 0;       // bytes from offset, within the pinned pages
    };

    /// A pinned entry list plus the position of the operation inside it.
    class iovec {
        public:
        /// Entries per operation.  A readv()/writev() longer than this is
        /// refused with EINVAL, not truncated.
        static constexpr usize max_segments = 8;

        iovec()                        = default;
        iovec(const iovec&)            = delete;
        iovec& operator=(const iovec&) = delete;

        /// The cursor travels with the frames it walks: a slot hands the pair
        /// to the reaper, and reference-counted state must never have two
        /// owners.
        iovec(iovec&& other) noexcept;
        iovec& operator=(iovec&& other) noexcept;

        /// Reference one more entry.  A zero `length` is legal and dropped.
        /// False only on a hard failure — no room left, or the frames cannot
        /// be referenced — and then nothing is kept.  `destination` carries
        /// the same meaning per entry that it does for pinned_buffer.
        bool append(phys_region* region, usize first, usize count, usize offset,
                    usize length, bool destination);

        /// Reference one more entry whose bytes are already kernel memory — an
        /// in-kernel caller's buffer, which cannot be unmapped and so needs no
        /// pin.  Such a caller can still park, so its operation walks the same
        /// cursor as everyone else's rather than needing a second engine path.
        bool append_kernel(void* address, usize length);

        /// Drop every reference.  Safe and idempotent on an empty list, and
        /// task context only: it may take a lock and may free a region.
        void release();

        bool  empty() const;
        usize count() const;
        usize bytes() const; // total referenced bytes, the sum of the lengths

        // ---- the cursor: where the operation is inside that list ----

        /// Point at the first byte and walk at most `length` bytes: the
        /// operation may be shorter than the list.  A list that is never aimed
        /// walks everything it holds, because appending arms what it appended;
        /// aim() is for starting over, or for deliberately walking less.
        void aim(usize length);

        usize remaining() const;
        bool  done() const; // nothing left to move

        /// Kernel address of the next byte, or nullptr when done.  Direct
        /// mapped, so it stays valid when the operation resumes in IRQ
        /// context.
        void* window() const;

        u64 physical() const;

        /// Bytes contiguous from window() before the entry or the operation
        /// ends, whichever comes first.
        usize chunk() const;

        /// Consume up to `bytes`; returns how many were consumed, so the
        /// caller's count and the cursor cannot drift apart.
        usize advance(usize bytes);

        private:
        pinned_segment segments_[max_segments];
        usize          count_     = 0;
        usize          total_     = 0;
        usize          index_     = 0;
        usize          offset_    = 0;
        usize          remaining_ = 0;
    };

} // namespace astia::mm
