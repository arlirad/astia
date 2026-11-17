#pragma once

#include <astia/common.hpp>

namespace astia::proc::detail {

    using rollback_unmap_fn = void (*)(void*, u64, usize);
    using rollback_release_frames_fn = void (*)(void*, u64, u64);
    using rollback_release_region_fn = void (*)(void*);

    inline void rollback_mapping(void* context, u64 address, usize pages, u64 frames,
                                 void* region, rollback_unmap_fn unmap,
                                 rollback_release_frames_fn release_frames,
                                 rollback_release_region_fn release_region) {
        unmap(context, address, pages);
        if (region != nullptr)
            release_region(region);
        else
            release_frames(context, frames, pages);
    }

} // namespace astia::proc::detail
