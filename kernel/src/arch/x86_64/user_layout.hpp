#pragma once

#include <astia/common.hpp>

namespace astia::arch::x86_64 {

    struct user_layout_policy {
        inline static constexpr u32 virtual_address_bits = 48;

        inline static constexpr u64 image_base       = 0x0000000040000000ull;
        inline static constexpr u64 address_limit     = 0x0000800000000000ull;
        inline static constexpr u64 mmap_top         = 0x00007fffe0000000ull;
        inline static constexpr u64 stack_top        = 0x00007ffffff000ull;
        inline static constexpr u64 tls_base         = 0x00007ffff0000000ull;
        inline static constexpr u64 process_tls_first = 0x00007fffef000000ull;
        inline static constexpr u64 process_tls_stride = 0x00100000ull;
        inline static constexpr u64 ring_base        = 0x0000700000000000ull;
        inline static constexpr u64 ring_slot_span   = 0x000000010000ull;
        inline static constexpr u64 tls_tcb_headroom = 256;

        static constexpr bool is_canonical(u64 address) {
            constexpr u64 low_mask = (u64{1} << virtual_address_bits) - 1;
            constexpr u64 sign_bit = u64{1} << (virtual_address_bits - 1);
            const u64 low = address & low_mask;
            const u64 high = address & ~low_mask;
            return (high == 0 && (low & sign_bit) == 0) ||
                   (high == ~low_mask && (low & sign_bit) != 0);
        }
    };

} // namespace astia::arch::x86_64
