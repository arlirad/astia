#include "gic.hpp"
#include "test.hpp"

namespace {
    using namespace astia;
    namespace gicv3 = astia::arch::aarch64::gicv3;
} // namespace

ASTIA_TEST(aarch64_gic_self_sgi_encoding) {
    ASTIA_CHECK(gicv3::self_sgi_id <= gicv3::max_sgi_id);
    ASTIA_CHECK(gicv3::self_sgi1r_value(0, gicv3::self_sgi_id) ==
                ((u64{gicv3::self_sgi_id} << 24) | 1ULL));
    ASTIA_CHECK(gicv3::self_sgi1r_value(0x01020304, gicv3::self_sgi_id) ==
                ((1ULL << 4) | (3ULL << 16) | (u64{gicv3::self_sgi_id} << 24) |
                 (2ULL << 32) | (1ULL << 48)));
}

ASTIA_TEST(aarch64_gic_mpidr_affinity) {
    ASTIA_CHECK(gicv3::affinity_from_mpidr(0x80000000ULL) == 0);
    ASTIA_CHECK(gicv3::affinity_from_mpidr(0x0000000001020304ULL) == 0x00020304U);
    ASTIA_CHECK(gicv3::affinity_from_mpidr(0x000000ff01020304ULL) == 0xff020304U);
}

ASTIA_TEST(aarch64_gic_irq_policy) {
    ASTIA_CHECK(gicv3::classify_intid(gicv3::self_sgi_id) == gicv3::irq_action::handled);
    ASTIA_CHECK(gicv3::classify_intid(0) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(14) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(16) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(32) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1019) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1020) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1021) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1022) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1023) == gicv3::irq_action::spurious);
    ASTIA_CHECK(gicv3::classify_intid(0xffffff) == gicv3::irq_action::fatal);
}

ASTIA_TEST(aarch64_gic_register_policy) {
    ASTIA_CHECK(gicv3::redistributor_stride == 0x20000ULL);
    ASTIA_CHECK(gicv3::sgi_base_offset == 0x10000ULL);
    ASTIA_CHECK(gicv3::minimum_distributor_size == 0x10000ULL);
    ASTIA_CHECK(gicv3::minimum_redistributor_size == 0x20000ULL);
    ASTIA_CHECK(gicv3::gicd_ctlr_enable_grp1ns == 0x2U);
    ASTIA_CHECK(gicv3::gicd_ctlr_are_ns == 0x10U);
    ASTIA_CHECK(gicv3::gicd_ctlr_rwp == 0x80000000U);
    ASTIA_CHECK(gicv3::gicr_waker_processor_sleep == 0x2U);
    ASTIA_CHECK(gicv3::gicr_waker_children_asleep == 0x4U);
    ASTIA_CHECK(gicv3::gicr_typer_last == 0x10U);
    ASTIA_CHECK(gicv3::gicr_priority == 0xa0U);
    ASTIA_CHECK(gicv3::pmr_allow_priorities == 0xf0U);
    ASTIA_CHECK(gicv3::bpr1_no_grouping == 0U);
    ASTIA_CHECK(gicv3::icc_ctlr_eoimode == 0x2U);
    ASTIA_CHECK(gicv3::icc_sre_sre == 0x1U);
    ASTIA_CHECK(gicv3::icc_igrpen1_enable == 0x1U);
}

ASTIA_TEST(aarch64_gic_region_policy) {
    ASTIA_CHECK(gicv3::valid_region({0x08000000ULL, 0x10000ULL}));
    ASTIA_CHECK(gicv3::valid_region({0x080a0000ULL, 0xf60000ULL}));
    ASTIA_CHECK(!gicv3::valid_region({0x08000000ULL, 0}));
    ASTIA_CHECK(!gicv3::valid_region({0x08000001ULL, 0x1000ULL}));
    ASTIA_CHECK(!gicv3::valid_region({0x08000000ULL, 0x1001ULL}));
    ASTIA_CHECK(!gicv3::valid_region({~0ULL - 0xfffULL, 0x2000ULL}));
}
