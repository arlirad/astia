#pragma once

#include <astia/common.hpp>

namespace astia::proc::detail {

    using rollback_unmap_fn = void (*)(void*, u64, usize);
    using rollback_release_frames_fn = void (*)(void*, u64, u64);
    using rollback_release_region_fn = void (*)(void*);

    struct rollback_mapping_entry {
        u64   address = 0;
        usize pages = 0;
        u64   frames = 0;
        void* region = nullptr;
        bool* mapped = nullptr;
        bool  release_backing = false;
    };

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

    inline void rollback_registered_mappings(
        void* context, rollback_mapping_entry* entries, usize count,
        rollback_unmap_fn unmap, rollback_release_frames_fn release_frames,
        rollback_release_region_fn release_region) {
        for (usize i = 0; i < count; ++i) {
            auto& entry = entries[i];
            if (entry.mapped == nullptr || !*entry.mapped)
                continue;
            unmap(context, entry.address, entry.pages);
            *entry.mapped = false;
            if (!entry.release_backing)
                continue;
            if (entry.region != nullptr)
                release_region(entry.region);
            else
                release_frames(context, entry.frames, entry.pages);
        }
    }

} // namespace astia::proc::detail
