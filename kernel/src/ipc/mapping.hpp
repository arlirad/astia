#pragma once

#include <astia/mapping.hpp>

#include "futex.hpp"

namespace astia {
    template <>
    struct mapper<ipc::futex_operation> {
        static optional<ipc::futex_operation> from(u32 raw) {
            if (raw > 1)
                return optional<ipc::futex_operation>(error_t::einval);

            return optional<ipc::futex_operation>(static_cast<ipc::futex_operation>(raw));
        }
    };
} // namespace astia
