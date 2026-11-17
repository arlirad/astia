#pragma once

#include <astia/arch/user_layout.hpp>
#include <astia/common.hpp>

namespace astia::proc {

    using user_layout_policy = arch::user_layout_policy;

    inline constexpr u32 user_virtual_address_bits =
        user_layout_policy::virtual_address_bits;
    inline constexpr u64 user_image_base    = user_layout_policy::image_base;
    inline constexpr u64 user_address_limit = user_layout_policy::address_limit;
    inline constexpr u64 user_mmap_top      = user_layout_policy::mmap_top;
    inline constexpr u64 user_stack_top     = user_layout_policy::stack_top;
    inline constexpr u64 user_tls_base      = user_layout_policy::tls_base;
    inline constexpr u64 process_tls_first  = user_layout_policy::process_tls_first;
    inline constexpr u64 process_tls_stride = user_layout_policy::process_tls_stride;
    inline constexpr u64 user_stack_bytes             = 256 * 1024;
    inline constexpr u64 thread_stack_alignment_bytes = 16;
    inline constexpr u64 thread_stack_alignment_mask  = thread_stack_alignment_bytes - 1;

    inline constexpr u64 user_ring_base      = user_layout_policy::ring_base;
    inline constexpr u64 user_ring_slot_span = user_layout_policy::ring_slot_span;
    inline constexpr u64 user_tls_tcb_headroom = user_layout_policy::tls_tcb_headroom;

    constexpr bool is_user_address_canonical(u64 address) {
        return user_layout_policy::is_canonical(address);
    }

    constexpr bool is_user_address_range(u64 address, u64 bytes = 1) {
        return bytes != 0 && address < user_address_limit &&
               bytes - 1 <= user_address_limit - 1 - address &&
               is_user_address_canonical(address) &&
               is_user_address_canonical(address + bytes - 1);
    }

    static_assert(user_image_base < user_mmap_top);
    static_assert(user_mmap_top < user_address_limit);
    static_assert(user_stack_top < user_address_limit);
    static_assert(user_stack_bytes != 0 && (user_stack_bytes % mm::page_size) == 0);
    static_assert(user_stack_top >= user_stack_bytes);
    static_assert(user_ring_slot_span != 0 &&
                  (user_ring_base % user_ring_slot_span) == 0);
    static_assert(user_ring_base + user_ring_slot_span <= user_address_limit);

} // namespace astia::proc
