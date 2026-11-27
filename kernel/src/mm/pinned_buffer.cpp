#include <astia/mm/pinned_buffer.hpp>

namespace astia::mm {
    pinned_buffer::pinned_buffer(pinned_buffer&& other) noexcept
        : _region(other._region), _first(other._first), _count(other._count),
          _destination(other._destination) {
        other._region      = nullptr;
        other._first       = 0;
        other._count       = 0;
        other._destination = false;
    }

    pinned_buffer& pinned_buffer::operator=(pinned_buffer&& other) noexcept {
        if (this != &other) {
            release();

            _region            = other._region;
            _first             = other._first;
            _count             = other._count;
            _destination       = other._destination;
            other._region      = nullptr;
            other._first       = 0;
            other._count       = 0;
            other._destination = false;
        }

        return *this;
    }

    bool pinned_buffer::acquire(phys_region* region, usz first, usz count,
                                bool destination) {
        if (region == nullptr || count == 0 || first >= region->pages() ||
            count > region->pages() - first)
            return false;

        // One call, two references: share_range() bumps the allocation
        // reference and the per-page mapping coverage together, and fails
        // without taking either when a counter would overflow.
        if (!region->share_range(first, count))
            return false;

        // Coverage keeps the frames allocated but does not stop a CoW
        // split from moving the caller's mapping to a copy; a destination
        // pin adds that, because the device is about to write these
        // frames and the caller must read those bytes back.
        if (destination && !region->retain_destination(first, count)) {
            region->release_range(first, count);

            return false;
        }

        _region      = region;
        _first       = first;
        _count       = count;
        _destination = destination;

        return true;
    }

    void pinned_buffer::release() {
        // Clear first.  release_range() may free the region object — the pin
        // can be its last reference — so nothing here may touch it after.
        phys_region* region = _region;

        const usz  first       = _first;
        const usz  count       = _count;
        const bool destination = _destination;

        _region      = nullptr;
        _first       = 0;
        _count       = 0;
        _destination = false;

        if (region != nullptr) {
            if (destination)
                region->release_destination(first, count);

            region->release_range(first, count);
        }
    }

    void* pinned_buffer::direct(usz index) const {
        return has_page(index) ? _region->page_direct(_first + index) : nullptr;
    }

    bool pinned_buffer::contiguous() const {
        if (_region == nullptr || _count < 2)
            return true;

        u64 prior = _region->page_base(_first);

        for (usz i = 1; i < _count; ++i) {
            const u64 current = _region->page_base(_first + i);

            if (current != prior + phys_region::frame_size)
                return false;

            prior = current;
        }

        return true;
    }

    u64 pinned_buffer::phys(usz index) const {
        if (!has_page(index))
            return frame_allocator::invalid_frame;

        return _region->base() +
               static_cast<u64>(_first + index) * phys_region::frame_size;
    }
} // namespace astia::mm
