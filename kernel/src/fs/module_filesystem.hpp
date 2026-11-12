#pragma once

#include <astia/common.hpp>

namespace astia::fs {

    inline constexpr u32 module_filesystem_name_max = 32;
    inline constexpr u32 invalid_module_filesystem_slot = ~u32{0};

    struct module_filesystem_registration {
        u32 slot       = invalid_module_filesystem_slot;
        u32 generation = 0;

        constexpr bool valid() const {
            return slot != invalid_module_filesystem_slot && generation != 0;
        }
    };

    long register_module_filesystem(const char* name,
                                    module_filesystem_registration& registration);
    long unregister_module_filesystem(module_filesystem_registration& registration);
    usize module_filesystem_count();

} // namespace astia::fs
