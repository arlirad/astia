#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::mm {

    /// A contiguous run of physical frames, with exact per-page mapping
    /// coverage.  The allocation-level reference count is a cheap
    /// sharing hint; page_refs_ is authoritative for CoW and reclamation.
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
        static constexpr usize frame_size = frame_allocator::frame_size;

        /// Allocate `page_count` frames (zeroed when `zero`) and adopt
        /// them.  Returns nullptr — frames released again — on
        /// failure; the caller owns the single initial reference.
        static phys_region* create(usize page_count, bool zero);

        /// Adopt an already-allocated, contiguous frame run: `base` is
        /// its physical base and ownership transfers — the last
        /// release frees the frames like an allocated region.  Callers
        /// that mapped the frames themselves (exec segments, the user
        /// stack, the TLS block) file them this way so fork and CoW
        /// account for them like any other mapping.  nullptr — frames
        /// stay the caller's — on failure.
        static phys_region* adopt(u64 base, usize page_count);

        phys_region(const phys_region&)            = delete;
        phys_region& operator=(const phys_region&) = delete;

        bool addref();

        /// Add one mapping reference covering [first, first + count).
        /// Unlike addref(), this supports a shared subrange of a larger
        /// allocation.  The object reference and page counters change
        /// together; false means a checked counter would overflow.
        bool share_range(usize first, usize count);

        /// Change only page coverage, without changing the allocation's
        /// object reference.  Used while one held VMA reference is
        /// divided into VMA siblings.
        bool retain_mapping(usize first, usize count);
        bool unmap_range(usize first, usize count);

        /// Drop one mapping reference covering a range.
        bool release_range(usize first, usize count);

        /// Drop only the allocation reference.  All page coverage must
        /// already have been removed by unmap_range().
        void release_ref();

        /// Drop one reference.  The last one unlinks the region from
        /// its parent, detaches its children (they own their own
        /// frames — the parent link is accounting, not ownership),
        /// returns the frames to the allocator and frees the object.
        void release();

        u64 base() const {
            return base_;
        }
        usize pages() const {
            return pages_;
        }

        /// Current reference count — introspection for tests.
        u32 references();

        /// Coarse allocation-level sharing count used as a fast path;
        /// page coverage remains authoritative.
        u32 mapping_references() {
            return references();
        }

        /// Direct-map address of page `index`: the window through
        /// which CoW copies read and write page bytes.
        void* page_direct(usize index);

        /// Copy page `index` into a fresh single-page child region and
        /// record the child against this parent.  The caller maps the
        /// child from here on.  nullptr on allocation failure.
        phys_region* split_page(usize index);

        /// True when page `index` has exactly one mapping left among
        /// this region's sharers — the caller's own — so writing it in
        /// place is safe.
        bool uniquely_held(usize index);

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
        bool initialize(u64 base, usize pages);

        core::spinlock lock_{};
        u64            base_        = 0;
        usize          pages_       = 0;
        u32            refs_        = 1;
        u16*           page_refs_   = nullptr;
        child*         children_    = nullptr;
        phys_region*   parent_      = nullptr;
        usize          parent_page_ = 0;
    };

} // namespace astia::mm
