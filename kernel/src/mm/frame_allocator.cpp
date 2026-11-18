#include <astia/memory.hpp>
#include <astia/mm/frame_allocator.hpp>

// Provided by the linker script (cmake/linker/virt-x86_64.ld).
extern "C" char __kernel_start[];
extern "C" char __kernel_end[];

namespace astia::mm {

    namespace {
        void* identity_access(u64 physical) {
            return reinterpret_cast<void*>(static_cast<usize>(physical));
        }
    }

    void frame_allocator::init(const memory_map& map) {
        core::spinlock_guard guard(_lock);

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
        _frame_count = limit / frame_size;
        _words       = (_frame_count + 63) / 64;

        // Park the bitmap directly past the kernel image; the kernel range
        // is reserved below, so the bitmap is never handed out.
        const u64 bitmap_base = align_up(reinterpret_cast<u64>(__kernel_end), frame_size);
        _bitmap_physical      = bitmap_base;
        if (_access == nullptr)
            _access = identity_access;
        // Access hooks can be arch callbacks (and may take paging locks), so
        // never invoke one while holding the allocator lock.  The allocator
        // is not published until the bitmap has been initialized below.
        const frame_access_fn access = _access;
        guard.unlock();
        u64* const           bitmap = static_cast<u64*>(access(bitmap_base));
        core::spinlock_guard publish(_lock);
        _bitmap = bitmap;

        for (usize i = 0; i < _words; ++i) {
            _bitmap[i] = ~u64{0};
        }
        _free_count = 0;

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
        mark_range(bitmap_base, _words * sizeof(u64), true);

        _ready = true;
    }

    void frame_allocator::configure_access(frame_access_fn access) {
        core::spinlock_guard  guard(_lock);
        const frame_access_fn selected = access == nullptr ? identity_access : access;
        const u64             physical = _bitmap_physical;
        guard.unlock();

        // As in init(), resolve the physical address without the allocator
        // lock: an arch access hook is external code and must not be called
        // in the allocator's critical section.
        u64* bitmap = physical == 0 ? nullptr : static_cast<u64*>(selected(physical));

        core::spinlock_guard publish(_lock);
        _access = selected;
        if (physical != 0) {
            _bitmap = bitmap;
        }
    }

    void frame_allocator::reserve(u64 physical, u64 size) {
        core::spinlock_guard guard(_lock);
        if (_ready)
            mark_range(physical, size, true);
    }

    bool frame_allocator::is_used(usize index) const {
        return (_bitmap[index / 64] >> (index % 64)) & u64{1};
    }

    void frame_allocator::set_used(usize index, bool used) {
        u64&       word     = _bitmap[index / 64];
        const u64  mask     = u64{1} << (index % 64);
        const bool was_used = (word & mask) != 0;
        if (used == was_used) {
            return;
        }
        if (used) {
            word |= mask;
            --_free_count;
        }
        else {
            word &= ~mask;
            ++_free_count;
        }
    }

    void frame_allocator::mark_range(u64 base, u64 size, bool used) {
        if (size == 0) {
            return;
        }
        const u64 first = base / frame_size;
        const u64 last  = (base + size + frame_size - 1) / frame_size;
        const u64 stop  = last < _frame_count ? last : _frame_count;
        for (u64 index = first; index < stop; ++index) {
            set_used(static_cast<usize>(index), used);
        }
    }

    u64 frame_allocator::alloc(usize count, frame_flags flags) {
        if (count == 0) {
            return invalid_frame;
        }
        core::spinlock_guard guard(_lock);
        if (!_ready || count > _free_count) {
            return invalid_frame;
        }

        // Find the first run of `count` free frames.
        usize found = 0;
        usize run   = 0;
        bool  have  = false;
        for (usize index = 0; index < _frame_count; ++index) {
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
            // The used bits reserve these frames until the caller receives
            // them. Copy the access hook under the bitmap lock, then keep the
            // potentially slow callback outside it.
            const frame_access_fn access = _access;
            guard.unlock();
            fill_zero(access(frame), static_cast<usize>(count) * frame_size);
        }
        return frame;
    }

    void frame_allocator::release(u64 frame, usize count) {
        core::spinlock_guard guard(_lock);
        if (!_ready) {
            return;
        }
        const usize first = static_cast<usize>(frame / frame_size);
        for (usize index = first; index < first + count && index < _frame_count;
             ++index) {
            set_used(index, false);
        }
    }

    frame_allocator& kernel_frames() {
        static frame_allocator instance;
        return instance;
    }

} // namespace astia::mm
