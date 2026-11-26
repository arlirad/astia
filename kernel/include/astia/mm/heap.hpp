#pragma once

#include <astia/common.hpp>
#include <astia/optional.hpp>

namespace astia::mm::heap {
    /// How the arch makes a heap page reachable: a plain function pointer
    /// (the project convention — no interface to implement), so mm never
    /// sees the page map itself. Returns false if the mapping failed.
    using map_fn = bool (*)(u64 virt, u64 phys);

    /// The heap's virtual window size; the arch paging step publishes the
    /// window address (image window, right after the kernel image).
    inline constexpr bytes_t region_bytes = 8_mib;

    /// Backing-page size used while wiring up the region.
    inline constexpr usize page_bytes = page_size;

    /// Register the region. The arch paging step calls this before init();
    /// init() then backs `base` page by page through `map`, taking the
    /// frames from the kernel frame allocator.
    void configure(void* base, usize bytes, map_fn map);

    /// Back the region with frames and build the free list. Registered as a
    /// DAG step (needs "paging"); also callable directly in tests.
    bool init();

    bool ready();

    /// Allocate `bytes` (16-byte aligned) or say why not: enomem when the
    /// heap cannot satisfy the request (including before init() ran),
    /// einval for a zero size. The optional-returning shape is the point —
    /// an allocation failure is the caller's to handle, not a kernel panic.
    optional<u8*> alloc(usize bytes);

    /// Like alloc(), but zeroes the payload.
    optional<u8*> alloc_zeroed(usize bytes);

    /// Release a payload from alloc()/alloc_zeroed(). Pointers outside the
    /// region and double frees are ignored rather than corrupting the heap.
    void free(void* ptr);

    /// Payload bytes currently handed out to callers (excludes headers).
    usize used();

    /// Payload bytes the region can ever hand out (excludes one header).
    usize capacity();
} // namespace astia::mm::heap
