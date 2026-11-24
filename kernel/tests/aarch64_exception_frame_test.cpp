#include <astia/arch/exception_frame.hpp>

#include "test.hpp"

#include <cstddef>

namespace {
    using astia::arch::aarch64::exception_frame;
    namespace layout = astia::arch::aarch64;
} // namespace

ASTIA_TEST(aarch64_exception_frame_layout) {
    ASTIA_CHECK(sizeof(exception_frame) == 0x120);
    ASTIA_CHECK(alignof(exception_frame) == 8);
    ASTIA_CHECK(offsetof(exception_frame, x) == 0x000);
    ASTIA_CHECK(offsetof(exception_frame, x[30]) == 0x0f0);
    ASTIA_CHECK(offsetof(exception_frame, elr_el1) == 0x0f8);
    ASTIA_CHECK(offsetof(exception_frame, spsr_el1) == 0x100);
    ASTIA_CHECK(offsetof(exception_frame, esr_el1) == 0x108);
    ASTIA_CHECK(offsetof(exception_frame, far_el1) == 0x110);
    ASTIA_CHECK(offsetof(exception_frame, reserved) == 0x118);

    ASTIA_CHECK(layout::exception_frame_gpr_count == 31);
    ASTIA_CHECK(layout::exception_frame_x_offset == offsetof(exception_frame, x));
    ASTIA_CHECK(layout::exception_frame_elr_offset == offsetof(exception_frame, elr_el1));
    ASTIA_CHECK(layout::exception_frame_spsr_offset ==
                offsetof(exception_frame, spsr_el1));
    ASTIA_CHECK(layout::exception_frame_esr_offset == offsetof(exception_frame, esr_el1));
    ASTIA_CHECK(layout::exception_frame_far_offset == offsetof(exception_frame, far_el1));
    ASTIA_CHECK(layout::exception_frame_rsvd_offset ==
                offsetof(exception_frame, reserved));
    ASTIA_CHECK(layout::exception_frame_size == sizeof(exception_frame));
    ASTIA_CHECK((layout::exception_frame_size % 16) == 0);
}

ASTIA_TEST(aarch64_exception_vector_layout) {
    ASTIA_CHECK(layout::exception_vector_slot_size == 0x80);
    ASTIA_CHECK(layout::exception_vector_table_size == 0x800);
    ASTIA_CHECK(layout::exception_vector_table_alignment == 0x800);
    ASTIA_CHECK(layout::exception_vector_current_el_spx_sync_offset == 0x200);
    ASTIA_CHECK(layout::exception_vector_current_el_spx_sync ==
                layout::exception_vector_current_el_spx_sync_offset /
                    layout::exception_vector_slot_size);
    ASTIA_CHECK(layout::exception_vector_current_el_spx_irq_offset == 0x280);
    ASTIA_CHECK(layout::exception_vector_current_el_spx_irq ==
                layout::exception_vector_current_el_spx_irq_offset /
                    layout::exception_vector_slot_size);
}
