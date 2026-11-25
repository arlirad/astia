#include "gic.hpp"
#include "test.hpp"
#include "timer.hpp"

namespace {
    using namespace astia;
    namespace gicv3 = astia::arch::aarch64::gicv3;
    namespace timer = astia::arch::aarch64::timer;

    void count_local_irq(void* context) {
        ++*static_cast<u32*>(context);
    }
} // namespace

ASTIA_TEST(aarch64_gic_local_intid_validity) {
    ASTIA_CHECK(gicv3::local_intid_limit == 32U);
    ASTIA_CHECK(gicv3::valid_local_intid(0));
    ASTIA_CHECK(gicv3::valid_local_intid(gicv3::self_sgi_id));
    ASTIA_CHECK(gicv3::valid_local_intid(timer::ppi_id));
    ASTIA_CHECK(gicv3::valid_local_intid(31));
    ASTIA_CHECK(!gicv3::valid_local_intid(32));
    ASTIA_CHECK(!gicv3::valid_local_intid(1020));
    ASTIA_CHECK(!gicv3::valid_local_intid(gicv3::spurious_intid));
    ASTIA_CHECK(!gicv3::valid_local_intid(0xffffff));
}

ASTIA_TEST(aarch64_gic_local_registration_policy) {
    u32 calls = 0;
    ASTIA_CHECK(gicv3::register_local_irq(32, count_local_irq, &calls) ==
                gicv3::registration_error::invalid_intid);
    ASTIA_CHECK(
        gicv3::register_local_irq(gicv3::spurious_intid, count_local_irq, &calls) ==
        gicv3::registration_error::invalid_intid);
    ASTIA_CHECK(gicv3::register_local_irq(0, nullptr, nullptr) ==
                gicv3::registration_error::null_handler);
    ASTIA_CHECK(gicv3::register_local_irq(6, count_local_irq, &calls) ==
                gicv3::registration_error::none);
    ASTIA_CHECK(gicv3::register_local_irq(6, count_local_irq, &calls) ==
                gicv3::registration_error::already_registered);
    ASTIA_CHECK(gicv3::classify_intid(6) == gicv3::irq_action::handled);
}

ASTIA_TEST(aarch64_gic_local_dispatch_policy) {
    ASTIA_CHECK(gicv3::classify_intid(7) == gicv3::irq_action::fatal);
    u32 calls = 0;
    ASTIA_CHECK(gicv3::register_local_irq(7, count_local_irq, &calls) ==
                gicv3::registration_error::none);
    ASTIA_CHECK(gicv3::classify_intid(7) == gicv3::irq_action::handled);
    ASTIA_CHECK(gicv3::classify_intid(8) == gicv3::irq_action::fatal);
}

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
    u32 calls = 0;
    ASTIA_CHECK(gicv3::register_local_irq(gicv3::self_sgi_id, count_local_irq, &calls) ==
                gicv3::registration_error::none);
    ASTIA_CHECK(gicv3::register_local_irq(timer::ppi_id, count_local_irq, &calls) ==
                gicv3::registration_error::none);
    ASTIA_CHECK(gicv3::classify_intid(gicv3::self_sgi_id) == gicv3::irq_action::handled);
    ASTIA_CHECK(gicv3::classify_intid(timer::ppi_id) == gicv3::irq_action::handled);
    ASTIA_CHECK(gicv3::classify_intid(0) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(14) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(16) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(29) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(31) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(32) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1019) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1020) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1021) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1022) == gicv3::irq_action::fatal);
    ASTIA_CHECK(gicv3::classify_intid(1023) == gicv3::irq_action::spurious);
    ASTIA_CHECK(gicv3::classify_intid(0xffffff) == gicv3::irq_action::fatal);
}

ASTIA_TEST(aarch64_timer_ppi_policy) {
    ASTIA_CHECK(timer::ppi_id == 30U);
    ASTIA_CHECK(timer::ctl_enable == 0x1U);
    ASTIA_CHECK(timer::ctl_imask == 0x2U);
    ASTIA_CHECK(timer::ctl_istatus == 0x4U);
    ASTIA_CHECK(timer::ctl_disabled_masked == timer::ctl_imask);
    ASTIA_CHECK(timer::one_shot_interval_ms == 10U);
    ASTIA_CHECK(timer::tval_max_positive == 0x7fffffffU);

    u32 ticks = 0;
    ASTIA_CHECK(timer::one_shot_ticks(62500000U, timer::one_shot_interval_ms, ticks));
    ASTIA_CHECK(ticks == 625000U);
    ASTIA_CHECK(timer::one_shot_ticks(1U, timer::one_shot_interval_ms, ticks));
    ASTIA_CHECK(ticks == 1U);
    ASTIA_CHECK(timer::one_shot_ticks(0xffffffffU, timer::one_shot_interval_ms, ticks));
    ASTIA_CHECK(ticks <= timer::tval_max_positive);
    ASTIA_CHECK(!timer::one_shot_ticks(0U, timer::one_shot_interval_ms, ticks));
    ASTIA_CHECK(!timer::one_shot_ticks(62500000U, 0U, ticks));
    ASTIA_CHECK(!timer::one_shot_ticks(0xffffffffU, 0xffffffffU, ticks));
}

ASTIA_TEST(aarch64_gic_register_policy) {
    ASTIA_CHECK(gicv3::redistributor_stride == 0x20000ULL);
    ASTIA_CHECK(gicv3::sgi_base_offset == 0x10000ULL);
    ASTIA_CHECK(gicv3::minimum_distributor_size == 0x10000ULL);
    ASTIA_CHECK(gicv3::minimum_redistributor_size == 0x20000ULL);
    ASTIA_CHECK(gicv3::gicd_ctlr_enable_grp0 == 0x1U);
    ASTIA_CHECK(gicv3::gicd_ctlr_enable_grp1ns == 0x2U);
    ASTIA_CHECK(gicv3::gicd_ctlr_group_enables == 0x3U);
    ASTIA_CHECK(gicv3::gicd_ctlr_are_ns == 0x10U);
    ASTIA_CHECK(gicv3::gicd_ctlr_ds == 0x40U);
    ASTIA_CHECK(gicv3::gicd_ctlr_rwp == 0x80000000U);
    ASTIA_CHECK(gicv3::gicr_waker_processor_sleep == 0x2U);
    ASTIA_CHECK(gicv3::gicr_waker_children_asleep == 0x4U);
    ASTIA_CHECK(gicv3::gicr_typer_last == 0x10U);
    ASTIA_CHECK(gicv3::gicr_priority == 0xa0U);
    ASTIA_CHECK(gicv3::pmr_allow_priorities == 0xf0U);
    ASTIA_CHECK(gicv3::bpr1_no_grouping == 0U);
    ASTIA_CHECK(gicv3::icc_ctlr_eoimode == 0x2U);
    ASTIA_CHECK(gicv3::icc_sre_sre == 0x1U);
    ASTIA_CHECK(gicv3::icc_sre_dfb == 0x2U);
    ASTIA_CHECK(gicv3::icc_sre_dib == 0x4U);
    ASTIA_CHECK(gicv3::icc_sre_bypass_disabled == 0x6U);
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
