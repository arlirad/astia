#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/memory_map.hpp>

namespace astia::mm {

    using frame_access_fn = void* (*) (u64 physical);

    /// Modifiers for frame_allocator::alloc().
    enum class frame_flags : u32 {
        none = 0,

        /// Require the frames to be physically contiguous. Multi-frame
        /// allocations are always contiguous (there is no way to describe a
        /// scattered result), so this is mostly for the single-frame case.
        contiguous = 1u << 0,

        /// Zero the returned frames before handing them back. Requires the
        /// frames to be reachable through the kernel's current page map.
        zeroed = 1u << 1,
    };

    constexpr frame_flags operator|(frame_flags a, frame_flags b) {
        return static_cast<frame_flags>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    constexpr bool has_flag(frame_flags value, frame_flags bit) {
        return (static_cast<u32>(value) & static_cast<u32>(bit)) != 0;
    }

    /// Physical frame (4 KiB page) allocator.
    ///
    /// Frames are tracked with one bitmap sized from the usable memory in a
    /// memory_map. This first cut serialises every operation with a single
    /// global spinlock; the interface is shaped so a per-CPU magazine can
    /// later sit in front of it without changing callers.
    class frame_allocator {
        public:
        static constexpr usize frame_size = page_size;

        static constexpr u64 invalid_frame = ~u64{0};

        /// Build the allocator from a memory map. Frames for the kernel
        /// image, the first MiB (firmware/BDA/EBDA/PVH option ROM) and the
        /// bitmap itself are reserved.
        void init(const memory_map& map);

        void reserve(u64 physical, u64 size);

        /// Allocate `count` contiguous frames. Returns the physical address
        /// of the first frame, or invalid_frame on failure.
        u64 alloc(usize count = 1, frame_flags flags = frame_flags::none);

        void configure_access(frame_access_fn access);

        /// Kernel virtual address for direct access to a physical frame.
        /// Identity before paging; the direct map afterwards.  Used to
        /// touch frames that are allocated but not yet mapped into any
        /// active address space (e.g. building a child's startup stack
        /// before its page map is ever activated).
        void* to_virt(u64 physical) const {
            return _access == nullptr
                       ? reinterpret_cast<void*>(static_cast<usize>(physical))
                       : _access(physical);
        }

        /// Release `count` frames starting at physical address `frame`.
        void release(u64 frame, usize count = 1);

        bool initialized() const {
            return _ready;
        }

        usize total_frames() const {
            return _frame_count;
        }

        usize available_frames() const {
            return _free_count;
        }

        private:
        bool is_used(usize index) const;
        void set_used(usize index, bool used);
        void mark_range(u64 base, u64 size, bool used);

        core::spinlock  _lock{};
        u64*            _bitmap          = nullptr;
        u64             _bitmap_physical = 0;
        frame_access_fn _access          = nullptr;
        usize           _words           = 0;
        usize           _frame_count     = 0;
        usize           _free_count      = 0;
        bool            _ready           = false;
    };

    /// The kernel's single physical frame allocator.
    frame_allocator& kernel_frames();

} // namespace astia::mm
