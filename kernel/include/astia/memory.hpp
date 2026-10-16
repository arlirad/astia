#pragma once

#include <astia/common.hpp>

// Standard memory primitives, defined in src/lib/memory.cpp.
//
// These have C linkage on purpose: besides being callable directly, they are
// exactly the symbols the compiler emits for aggregate copies/zeroing (e.g.
// a default-initialised array), so a freestanding kernel has to provide them
// or the final link fails.
extern "C" {
auto memset(void* dst, int value, astia::usize count) -> void*;
auto memcpy(void* dst, const void* src, astia::usize count) -> void*;
auto memmove(void* dst, const void* src, astia::usize count) -> void*;
auto memcmp(const void* lhs, const void* rhs, astia::usize count) -> int;
}

namespace astia {

    /// Round `value` up to the next multiple of `alignment` (a power of two).
    template <typename value_type, typename alignment_type>
    constexpr auto align_up(value_type value, alignment_type alignment) -> value_type {
        const value_type step = static_cast<value_type>(alignment);
        return static_cast<value_type>((value + step - 1) & ~(step - 1));
    }

    /// Round `value` down to a multiple of `alignment` (a power of two).
    template <typename value_type, typename alignment_type>
    constexpr auto align_down(value_type value, alignment_type alignment) -> value_type {
        const value_type step = static_cast<value_type>(alignment);
        return static_cast<value_type>(value & ~(step - 1));
    }

    /// Zero `count` bytes starting at `dst`.
    inline auto fill_zero(void* dst, usize count) -> void {
        memset(dst, 0, count);
    }

} // namespace astia
