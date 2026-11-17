#include "test.hpp"
#include "../src/proc/exec_rollback.hpp"

namespace {

    struct mapping {
        astia::u64 address;
        astia::usize pages;
        bool present;
    };

    struct state {
        mapping mappings[2]{};
        astia::u64 free_frames = 0;
        astia::u32 unmaps = 0;
        astia::u32 region_releases = 0;
        astia::u32 frame_releases = 0;
        astia::u32 order = 0;
        astia::u32 unmap_order = 0;
        astia::u32 release_order = 0;
    };

    void unmap(void* context, astia::u64 address, astia::usize pages) {
        auto& value = *static_cast<state*>(context);
        for (auto& mapping : value.mappings) {
            if (mapping.address != address)
                continue;
            ASTIA_CHECK(mapping.present);
            ASTIA_CHECK(mapping.pages == pages);
            mapping.present = false;
            value.unmap_order = ++value.order;
            ++value.unmaps;
            return;
        }
        ASTIA_CHECK(false);
    }

    void release_frames(void* context, astia::u64 frames, astia::u64 pages) {
        auto& value = *static_cast<state*>(context);
        ASTIA_CHECK(frames != 0);
        value.free_frames += pages;
        value.release_order = ++value.order;
        ++value.frame_releases;
    }

    void release_region(void* context) {
        auto& value = *static_cast<state*>(context);
        value.release_order = ++value.order;
        ++value.region_releases;
    }

} // namespace

ASTIA_TEST(exec_rollback_unmaps_before_releasing_raw_frames) {
    state value{{{0x1000, 2, true}, {0x3000, 1, true}}, 0};
    astia::proc::detail::rollback_mapping(
        &value, 0x1000, 2, 0x200000, nullptr, unmap, release_frames, release_region);
    ASTIA_CHECK(!value.mappings[0].present);
    ASTIA_CHECK(value.mappings[1].present);
    ASTIA_CHECK(value.unmaps == 1);
    ASTIA_CHECK(value.frame_releases == 1);
    ASTIA_CHECK(value.region_releases == 0);
    ASTIA_CHECK(value.unmap_order < value.release_order);
}

ASTIA_TEST(exec_rollback_unmaps_before_releasing_adopted_region) {
    state value{{{0x1000, 2, true}, {0x3000, 1, true}}, 0};
    astia::proc::detail::rollback_mapping(
        &value, 0x3000, 1, 0, &value, unmap, release_frames, release_region);
    ASTIA_CHECK(value.mappings[0].present);
    ASTIA_CHECK(!value.mappings[1].present);
    ASTIA_CHECK(value.unmaps == 1);
    ASTIA_CHECK(value.frame_releases == 0);
    ASTIA_CHECK(value.region_releases == 1);
    ASTIA_CHECK(value.unmap_order < value.release_order);
}
