#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::mm {
    struct phys_region_identity {};

    /// A contiguous run of physical frames, with exact per-page mapping
    /// coverage.  The allocation-level reference count is a cheap
    /// sharing hint; _page_refs is authoritative for CoW and reclamation.
    ///
    /// The metadata is allocation-local, not a kernel-wide frame table.
    /// A range share/unshare walks only the VMA's pages and updates their
    /// atomic u16 counters.  A page whose counter reaches zero is
    /// returned immediately, even while neighbouring pages remain live.
    ///
    /// Parent/child links remain lineage information for CoW copies;
    /// they are no longer used as a substitute for page coverage.
    class phys_region {
        public:
        static constexpr usz frame_size = frame_allocator::frame_size;

        /// Allocate `page_count` frames (zeroed when `zero`) and adopt
        /// them.  Returns nullptr — frames released again — on
        /// failure; the caller owns the single initial reference.
        static phys_region* create(usz page_count, bool zero);

        /// Reserve page metadata without allocating its frames.  Pages are
        /// materialized independently by ensure_page().
        static phys_region* create_lazy(usz page_count);

        /// Adopt an already-allocated, contiguous frame run: `base` is
        /// its physical base and ownership transfers — the last
        /// release frees the frames like an allocated region.  Callers
        /// that mapped the frames themselves (exec segments, the user
        /// stack, the TLS block) file them this way so fork and CoW
        /// account for them like any other mapping.  nullptr — frames
        /// stay the caller's — on failure.
        static phys_region* adopt(u64 base, usz page_count);

        phys_region(const phys_region&)            = delete;
        phys_region& operator=(const phys_region&) = delete;

        bool addref();

        /// Add one mapping reference covering [first, first + count).
        /// Unlike addref(), this supports a shared subrange of a larger
        /// allocation.  The object reference and page counters change
        /// together; false means a checked counter would overflow.
        bool share_range(usz first, usz count);

        /// Change only page coverage, without changing the allocation's
        /// object reference.  Used while one held VMA reference is
        /// divided into VMA siblings.
        bool retain_mapping(usz first, usz count);
        bool unmap_range(usz first, usz count);

        /// Drop one mapping reference covering a range.
        bool release_range(usz first, usz count);

        /// Drop only the allocation reference.  All page coverage must
        /// already have been removed by unmap_range().
        void release_ref();

        /// Drop one reference.  The last one unlinks the region from
        /// its parent, detaches its children (they own their own
        /// frames — the parent link is accounting, not ownership),
        /// returns the frames to the allocator and frees the object.
        void release();

        u64 base() const {
            return _base;
        }

        usz pages() const {
            return _pages;
        }

        /// True when pages are materialised individually on demand.
        bool lazy() const {
            return _page_bases != nullptr;
        }

        /// Physical base of a page, or invalid_frame when a lazy page has
        /// not been materialized yet.
        u64 page_base(usz index) const;

        /// Materialize one lazy page.  `created` reports whether this call
        /// allocated the frame rather than observing an existing one.
        bool ensure_page(usz index, bool zero, bool* created = nullptr);

        /// Current reference count — introspection for tests.
        u32 references();

        /// Coarse allocation-level sharing count used as a fast path;
        /// page coverage remains authoritative.
        u32 mapping_references() {
            return references();
        }

        shptr<phys_region_identity> identity() const {
            return _identity;
        }

        /// Direct-map address of page `index`: the window through
        /// which CoW copies read and write page bytes.
        void* page_direct(usz index);

        /// Copy page `index` into a fresh single-page child region and
        /// record the child against this parent.  The caller maps the
        /// child from here on.  nullptr on allocation failure.
        phys_region* split_page(usz index);

        phys_region* copy_page(usz index);

        /// True when page `index` has exactly one mapping left among
        /// this region's sharers — the caller's own — so writing it in
        /// place is safe.
        bool uniquely_held(usz index);

        /// Mark [first, first + count) as the *destination* of an
        /// in-flight operation: a device is about to write these frames
        /// and the caller will read the bytes back through this very
        /// mapping, so the frame must not be replaced underneath it.
        /// cow_fault() refuses to split a marked page because the
        /// operation's copy would land in a frame nobody reads.
        ///
        /// Counted, so overlapping pins each hold their own mark, and
        /// false — marking nothing — when the range escapes the region
        /// or a counter would overflow.
        bool retain_destination(usz first, usz count);

        /// Drop the marks retain_destination() took for the range.
        void release_destination(usz first, usz count);

        /// True while page `index` is the destination of an in-flight
        /// operation.  An operation that only *reads* the buffer (a
        /// write to the object) does not mark: a later split copies the
        /// same bytes, so it reads the caller's data either way.
        bool destination_pinned(usz index) const;

        private:
        phys_region() = default;

        /// One CoW child: a single-page region copied out of
        /// `parent_page` of its parent.  Nodes are intrusive list
        /// entries owned by the parent.
        struct child {
            child*       next        = nullptr;
            phys_region* region      = nullptr;
            u64          parent_page = 0;
        };

        void unlink_child(phys_region* target);
        void detach_parent(const phys_region* expected);
        bool initialize(u64 base, usz pages, bool lazy = false);

        core::spinlock              _lock{};
        u64                         _base       = 0;
        usz                         _pages      = 0;
        u32                         _refs       = 1;
        u16*                        _page_refs  = nullptr;
        u16*                        _dest_refs  = nullptr; ///< in-flight destination pins
        u64*                        _page_bases = nullptr; ///< non-null for lazy regions
        child*                      _children   = nullptr;
        phys_region*                _parent     = nullptr;
        usz                         _parent_page = 0;
        shptr<phys_region_identity> _identity;
    };
} // namespace astia::mm
