#pragma once

#include <astia/common.hpp>

namespace astia {

    /// Non-owning byte range. A null pointer is valid only for an empty range.
    struct string_view {
        const char* data = nullptr;
        usize       size = 0;

        constexpr bool valid() const {
            return data != nullptr || size == 0;
        }
    };

    constexpr bool string_equal(string_view lhs, string_view rhs) {
        if (!lhs.valid() || !rhs.valid() || lhs.size != rhs.size)
            return false;
        for (usize i = 0; i < lhs.size; ++i)
            if (lhs.data[i] != rhs.data[i])
                return false;
        return true;
    }

    /// Compare two NUL-terminated strings.  A null pointer is not equal to
    /// anything, including another null pointer.
    constexpr bool string_equal(const char* lhs, const char* rhs) {
        if (lhs == nullptr || rhs == nullptr)
            return false;

        while (*lhs != '\0' && *lhs == *rhs) {
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

    constexpr bool string_starts_with(string_view value, string_view prefix) {
        if (!value.valid() || !prefix.valid() || prefix.size > value.size)
            return false;
        for (usize i = 0; i < prefix.size; ++i)
            if (value.data[i] != prefix.data[i])
                return false;
        return true;
    }

    constexpr int string_compare(string_view lhs, string_view rhs) {
        if (!lhs.valid() || !rhs.valid()) {
            if (lhs.valid() == rhs.valid())
                return 0;
            return lhs.valid() ? 1 : -1;
        }
        const usize common = lhs.size < rhs.size ? lhs.size : rhs.size;
        for (usize i = 0; i < common; ++i) {
            const auto left  = static_cast<unsigned char>(lhs.data[i]);
            const auto right = static_cast<unsigned char>(rhs.data[i]);
            if (left != right)
                return left < right ? -1 : 1;
        }
        if (lhs.size == rhs.size)
            return 0;
        return lhs.size < rhs.size ? -1 : 1;
    }

    /// Extract bytes before the first NUL, without reading beyond the range.
    constexpr bool string_before_nul(string_view input, string_view& output) {
        if (!input.valid())
            return false;
        for (usize i = 0; i < input.size; ++i) {
            if (input.data[i] == '\0') {
                output = {input.data, i};
                return true;
            }
        }
        return false;
    }

} // namespace astia
