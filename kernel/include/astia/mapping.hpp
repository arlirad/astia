#pragma once

#include <astia/optional.hpp>

namespace astia {

    /// Compile-time selected conversion from an ABI scalar to a validated
    /// domain type. Subsystems provide specializations in their mapping.hpp.
    template <typename T>
    struct mapper;

    template <typename T, u64 ValidMask>
    struct bitmask_mapper {
        static optional<T> from(u64 raw) {
            if (raw & ~ValidMask)
                return optional<T>(error_t::einval);
            return optional<T>(static_cast<T>(raw));
        }
    };

} // namespace astia
