#pragma once

#include <astia/common.hpp>

namespace astia::proc {

    /// Process-wide userspace placement policy.  These are ABI/layout values,
    /// not architecture page-table constants.
    inline constexpr u64 user_image_base  = 0x0000000040000000ull;
    inline constexpr u64 user_address_limit = 0x0000800000000000ull;
    inline constexpr u64 user_mmap_top     = 0x00007fffe0000000ull;
    inline constexpr u64 user_stack_top    = 0x00007ffffff000ull;
    inline constexpr u64 user_stack_size   = 256 * 1024;

    /// Fixed per-process slots for the userspace aliases of AIO rings.
    inline constexpr u64 user_ring_base = 0x0000700000000000ull;
    inline constexpr u64 user_ring_slot_span = 0x000000010000ull;

    static_assert(user_image_base < user_mmap_top);
    static_assert(user_mmap_top < user_address_limit);
    static_assert(user_stack_top < user_address_limit);
    static_assert(user_stack_size != 0 &&
                  (user_stack_size % mm::page_size) == 0);
    static_assert(user_stack_top >= user_stack_size);
    static_assert(user_ring_slot_span != 0 &&
                  (user_ring_base % user_ring_slot_span) == 0);
    static_assert(user_ring_base + user_ring_slot_span <= user_address_limit);

} // namespace astia::proc
