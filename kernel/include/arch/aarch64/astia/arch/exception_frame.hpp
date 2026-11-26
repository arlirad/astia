#pragma once

#include <astia/common.hpp>

namespace astia::arch::aarch64 {
    /// EL1 exception-entry frame. The assembly in
    /// src/arch/aarch64/exception.S owns this layout; the C++ fatal reporter
    /// and the host layout test share these definitions.
    struct exception_frame {
        u64 x[31];
        u64 elr_el1;
        u64 spsr_el1;
        u64 esr_el1;
        u64 far_el1;
        u64 reserved;
    };

    inline constexpr u64 exception_frame_gpr_count   = 31;
    inline constexpr u64 exception_frame_x_offset    = 0x000;
    inline constexpr u64 exception_frame_elr_offset  = 0x0f8;
    inline constexpr u64 exception_frame_spsr_offset = 0x100;
    inline constexpr u64 exception_frame_esr_offset  = 0x108;
    inline constexpr u64 exception_frame_far_offset  = 0x110;
    inline constexpr u64 exception_frame_rsvd_offset = 0x118;
    inline constexpr u64 exception_frame_size        = 0x120;

    inline constexpr u64 exception_vector_slot_size       = 0x80;
    inline constexpr u64 exception_vector_table_size      = 0x800;
    inline constexpr u64 exception_vector_table_alignment = 0x800;

    inline constexpr u64 exception_vector_current_el_spx_sync_offset = 0x200;
    inline constexpr u64 exception_vector_current_el_spx_sync        = 4;
    inline constexpr u64 exception_vector_current_el_spx_irq_offset  = 0x280;
    inline constexpr u64 exception_vector_current_el_spx_irq         = 5;

    static_assert(sizeof(exception_frame) == exception_frame_size);
    static_assert(alignof(exception_frame) == 8);
    static_assert(exception_frame_size % 16 == 0);
} // namespace astia::arch::aarch64
