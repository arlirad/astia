#pragma once

#include <astia/common.hpp>

namespace astia::mod::detail {

    struct registration_slot_acquisition {
        u32  index;
        bool appended;
    };

    template <typename registration_type>
    registration_slot_acquisition acquire_registration_slot(registration_type* slots,
                                                            u32& count, u32 capacity) {
        for (u32 i = 0; i < count; ++i)
            if (slots[i].instance == nullptr && slots[i].ops == nullptr)
                return {i, false};
        if (count == capacity)
            return {capacity, false};
        return {count++, true};
    }

    inline void rollback_registration_slot(u32&                          count,
                                           registration_slot_acquisition acquisition) {
        if (acquisition.appended)
            --count;
    }

}
