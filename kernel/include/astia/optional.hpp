#pragma once

#include <astia/common.hpp>
#include <astia/error.hpp>

namespace astia {

    /// A value or an error: either an engaged `T` (`has_value == true`, with
    /// `error == error_t::enone`) or the `error_t` explaining why there is
    /// none.
    ///
    /// This is how fallible kernel allocations report failure. An exhausted
    /// heap or a failed `new` must not take the whole kernel down, so callers
    /// get a carrier they can inspect and recover from instead of a panic.
    template <typename T>
    struct optional {
        bool    has_value = false;
        error_t error     = error_t::enone;
        T       value{};

        optional() = default;

        /// Engaged value.
        optional(T v) : has_value(true), value(v) {}

        /// Failure, with a reason.
        optional(error_t e) : error(e) {}

        /// Engaged value plus a (usually non-fatal) status.
        optional(T v, error_t e) : has_value(true), error(e), value(v) {}

        explicit operator bool() const {
            return has_value;
        }

        /// The value, or `fallback` when disengaged.
        T value_or(T fallback) const {
            return has_value ? value : fallback;
        }
    };

} // namespace astia
