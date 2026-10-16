#include "frame_allocator.hpp"

#include <astia/memory.hpp>

// Provided by the linker script (cmake/linker/virt-x86_64.ld).
extern "C" char __kernel_start[];
extern "C" char __kernel_end[];

namespace astia::mm {

    namespace {
        auto identity_access(u64 physical) -> void* {
            return reinterpret_cast<void*>(static_cast<usize>(physical));
        }
    }

    auto frame_allocator::init(const memory_map& map) -> void {
        core::spinlock_guard guard(lock_);

        // Track everything up to the highest usable byte.
        u64 limit = 0;
        for (const auto& region : map) {
            if (region.type != memory_type::usable) {
                continue;
            }
            const u64 top = region.base + region.size;
            if (top > limit) {
                limit = top;
            }
        }
        frame_count_ = limit / frame_size;
        words_       = (frame_count_ + 63) / 64;

        // Park the bitmap directly past the kernel image; the kernel range
        // is reserved below, so the bitmap is never handed out.
        const u64 bitmap_base = align_up(reinterpret_cast<u64>(__kernel_end), frame_size);
        bitmap_physical_      = bitmap_base;
        if (access_ == nullptr)
            access_ = identity_access;
        bitmap_ = static_cast<u64*>(access_(bitmap_base));

        for (usize i = 0; i < words_; ++i) {
            bitmap_[i] = ~u64{0};
        }
        free_count_ = 0;

        // Everything usable is initially free ...
        for (const auto& region : map) {
            if (region.type == memory_type::usable) {
                mark_range(region.base, region.size, false);
            }
        }

        // ... then we take back what the kernel owns: RAM below 1 MiB
        // (IVT/BDA/EBDA, ROM and the PVH option ROM holding start_info),
        // the kernel image itself and the bitmap.
        mark_range(0x0, 0x100000, true);
        mark_range(reinterpret_cast<u64>(__kernel_start),
                   reinterpret_cast<u64>(__kernel_end) -
                       reinterpret_cast<u64>(__kernel_start),
                   true);
        mark_range(bitmap_base, words_ * sizeof(u64), true);

        ready_ = true;
    }

    auto frame_allocator::configure_access(frame_access_fn access) -> void {
        core::spinlock_guard guard(lock_);
        access_ = access == nullptr ? identity_access : access;
        if (bitmap_physical_ != 0) {
            bitmap_ = static_cast<u64*>(access_(bitmap_physical_));
        }
    }

    auto frame_allocator::is_used(usize index) const -> bool {
        return (bitmap_[index / 64] >> (index % 64)) & u64{1};
    }

    auto frame_allocator::set_used(usize index, bool used) -> void {
        u64&       word     = bitmap_[index / 64];
        const u64  mask     = u64{1} << (index % 64);
        const bool was_used = (word & mask) != 0;
        if (used == was_used) {
            return;
        }
        if (used) {
            word |= mask;
            --free_count_;
        }
        else {
            word &= ~mask;
            ++free_count_;
        }
    }

    auto frame_allocator::mark_range(u64 base, u64 size, bool used) -> void {
        if (size == 0) {
            return;
        }
        const u64 first = base / frame_size;
        const u64 last  = (base + size + frame_size - 1) / frame_size;
        const u64 stop  = last < frame_count_ ? last : frame_count_;
        for (u64 index = first; index < stop; ++index) {
            set_used(static_cast<usize>(index), used);
        }
    }

    auto frame_allocator::alloc(usize count, frame_flags flags) -> u64 {
        if (count == 0) {
            return invalid_frame;
        }
        core::spinlock_guard guard(lock_);
        if (!ready_ || count > free_count_) {
            return invalid_frame;
        }

        // Find the first run of `count` free frames.
        usize found = 0;
        usize run   = 0;
        bool  have  = false;
        for (usize index = 0; index < frame_count_; ++index) {
            if (is_used(index)) {
                run = 0;
                continue;
            }
            if (run == 0) {
                found = index;
            }
            if (++run == count) {
                have = true;
                break;
            }
        }
        if (!have) {
            return invalid_frame;
        }

        for (usize index = found; index < found + count; ++index) {
            set_used(index, true);
        }

        const u64 frame = static_cast<u64>(found) * frame_size;
        if (has_flag(flags, frame_flags::zeroed)) {
            fill_zero(access_(frame), static_cast<usize>(count) * frame_size);
        }
        return frame;
    }

    auto frame_allocator::release(u64 frame, usize count) -> void {
        core::spinlock_guard guard(lock_);
        if (!ready_) {
            return;
        }
        const usize first = static_cast<usize>(frame / frame_size);
        for (usize index = first; index < first + count && index < frame_count_;
             ++index) {
            set_used(index, false);
        }
    }

    auto kernel_frames() -> frame_allocator& {
        static frame_allocator instance;
        return instance;
    }

} // namespace astia::mm
