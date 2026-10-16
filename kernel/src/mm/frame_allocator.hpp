#pragma once

#include <astia/common.hpp>
#include "core/spinlock.hpp"
#include "memory_map.hpp"

namespace astia::mm {

    using frame_access_fn = void* (*)(u64 physical);

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

    constexpr auto operator|(frame_flags a, frame_flags b) -> frame_flags {
        return static_cast<frame_flags>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    constexpr auto has_flag(frame_flags value, frame_flags bit) -> bool {
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
        static constexpr usize frame_size = 4096;

        static constexpr u64 invalid_frame = ~u64{0};

        /// Build the allocator from a memory map. Frames for the kernel
        /// image, the first MiB (firmware/BDA/EBDA/PVH option ROM) and the
        /// bitmap itself are reserved.
        auto init(const memory_map& map) -> void;

        /// Allocate `count` contiguous frames. Returns the physical address
        /// of the first frame, or invalid_frame on failure.
        auto alloc(usize count = 1, frame_flags flags = frame_flags::none) -> u64;

        auto configure_access(frame_access_fn access) -> void;

        /// Kernel virtual address for direct access to a physical frame.
        /// Identity before paging; the direct map afterwards.  Used to
        /// touch frames that are allocated but not yet mapped into any
        /// active address space (e.g. building a child's startup stack
        /// before its page map is ever activated).
        auto to_virt(u64 physical) const -> void* {
            return access_ == nullptr
                       ? reinterpret_cast<void*>(static_cast<usize>(physical))
                       : access_(physical);
        }

        /// Release `count` frames starting at physical address `frame`.
        auto release(u64 frame, usize count = 1) -> void;

        auto initialized() const -> bool {
            return ready_;
        }

        auto total_frames() const -> usize {
            return frame_count_;
        }

        auto available_frames() const -> usize {
            return free_count_;
        }

        private:
        auto is_used(usize index) const -> bool;
        auto set_used(usize index, bool used) -> void;
        auto mark_range(u64 base, u64 size, bool used) -> void;

        core::spinlock lock_{};
        u64*           bitmap_      = nullptr;
        u64             bitmap_physical_ = 0;
        frame_access_fn access_ = nullptr;
        usize          words_       = 0;
        usize          frame_count_ = 0;
        usize          free_count_  = 0;
        bool           ready_       = false;
    };

    /// The kernel's single physical frame allocator.
    auto kernel_frames() -> frame_allocator&;

} // namespace astia::mm
