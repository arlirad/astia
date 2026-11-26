#pragma once

#include <astia/common.hpp>

// Standard memory primitives, defined in src/lib/memory.cpp.
//
// These have C linkage on purpose: besides being callable directly, they are
// exactly the symbols the compiler emits for aggregate copies/zeroing (e.g.
// a default-initialised array), so a freestanding kernel has to provide them
// or the final link fails.
extern "C" {
void* memset(void* dst, int value, astia::usize count);
void* memcpy(void* dst, const void* src, astia::usize count);
void* memmove(void* dst, const void* src, astia::usize count);
int   memcmp(const void* lhs, const void* rhs, astia::usize count);
}

namespace astia {
    /// Round `value` up to the next multiple of `alignment` (a power of two).
    template <typename value_type, typename alignment_type>
    constexpr value_type align_up(value_type value, alignment_type alignment) {
        const value_type step = static_cast<value_type>(alignment);

        return static_cast<value_type>((value + step - 1) & ~(step - 1));
    }

    /// Round `value` down to a multiple of `alignment` (a power of two).
    template <typename value_type, typename alignment_type>
    constexpr value_type align_down(value_type value, alignment_type alignment) {
        const value_type step = static_cast<value_type>(alignment);

        return static_cast<value_type>(value & ~(step - 1));
    }

    /// Zero `count` bytes starting at `dst`.
    inline void fill_zero(void* dst, usize count) {
        memset(dst, 0, count);
    }
} // namespace astia
