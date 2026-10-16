#include <astia/mm/pinned_buffer.hpp>

namespace astia::mm {

    bool pinned_buffer::acquire(phys_region* region, usize first, usize count) {
        if (region == nullptr || count == 0 || first >= region->pages() ||
            count > region->pages() - first) {
            return false;
        }
        // One call, two references: share_range() bumps the allocation
        // reference and the per-page mapping coverage together, and fails
        // without taking either when a counter would overflow.
        if (!region->share_range(first, count)) {
            return false;
        }
        region_ = region;
        first_  = first;
        count_  = count;
        return true;
    }

    void pinned_buffer::release() {
        // Clear first.  release_range() may free the region object — the pin
        // can be its last reference — so nothing here may touch it after.
        phys_region* region = region_;
        const usize  first  = first_;
        const usize  count  = count_;
        region_             = nullptr;
        first_              = 0;
        count_              = 0;
        if (region != nullptr) {
            region->release_range(first, count);
        }
    }

    void* pinned_buffer::direct(usize index) const {
        return region_ == nullptr ? nullptr : region_->page_direct(first_ + index);
    }

    u64 pinned_buffer::phys(usize index) const {
        return region_ == nullptr ? 0
                                  : region_->base() + static_cast<u64>(first_ + index) *
                                                          phys_region::frame_size;
    }

} // namespace astia::mm
