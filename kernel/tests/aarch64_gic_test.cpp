#include "gic.hpp"
#include "test.hpp"
#include "timer.hpp"

namespace {
    using namespace astia;
    namespace gicv3 = astia::arch::aarch64::gicv3;
    namespace irq   = astia::arch::aarch64::irq;
    namespace timer = astia::arch::aarch64::timer;

    void count_local_irq(void* context) {
        ++*static_cast<u32*>(context);
    }

    struct deadline_probe {
        u32 calls    = 0;
        u32 order[4] = {0, 0, 0, 0};
    };

    void record_deadline_a(void* context) {
        auto* probe = static_cast<deadline_probe*>(context);

        if (probe->calls < 4)
            probe->order[probe->calls] = 1;

        probe->calls = probe->calls + 1;
    }

    void record_deadline_b(void* context) {
        auto* probe = static_cast<deadline_probe*>(context);

        if (probe->calls < 4)
            probe->order[probe->calls] = 2;

        probe->calls = probe->calls + 1;
    }

    struct deadline_reentry_probe {
        u64 schedule_now = 0;
        u32 calls        = 0;
        u32 later_calls  = 0;
        u32 later_id     = 0;
    };

    void reschedule_deadline(void* context) {
        auto* probe = static_cast<deadline_reentry_probe*>(context);

        probe->calls = probe->calls + 1;

        u32 id = 0;

        if (timer::schedule_deadline(probe->schedule_now, 10, count_local_irq,
                                     &probe->later_calls,
                                     id) == timer::deadline_error::none)
            probe->later_id = id;
    }
} // namespace

ASTIA_TEST(aarch64_gic_self_sgi_encoding) {
    ASTIA_CHECK(gicv3::self_sgi_id <= irq::max_sgi_id);
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

ASTIA_TEST(aarch64_timer_ppi_policy) {
    ASTIA_CHECK(timer::ppi_id == 30U);
    ASTIA_CHECK(timer::ctl_enable == 0x1U);
    ASTIA_CHECK(timer::ctl_imask == 0x2U);
    ASTIA_CHECK(timer::ctl_istatus == 0x4U);
    ASTIA_CHECK(timer::ctl_disabled_masked == timer::ctl_imask);
    ASTIA_CHECK(timer::tick_interval_ms == 10U);
    ASTIA_CHECK(timer::tick_hz == 100U);
    ASTIA_CHECK(timer::tval_max_positive == 0x7fffffffU);

    u32 ticks = 0;

    ASTIA_CHECK(timer::interval_ticks(62500000U, timer::tick_interval_ms, ticks));
    ASTIA_CHECK(ticks == 625000U);
    ASTIA_CHECK(timer::interval_ticks(1U, timer::tick_interval_ms, ticks));
    ASTIA_CHECK(ticks == 1U);
    ASTIA_CHECK(timer::interval_ticks(0xffffffffU, timer::tick_interval_ms, ticks));
    ASTIA_CHECK(ticks <= timer::tval_max_positive);
    ASTIA_CHECK(!timer::interval_ticks(0U, timer::tick_interval_ms, ticks));
    ASTIA_CHECK(!timer::interval_ticks(62500000U, 0U, ticks));
    ASTIA_CHECK(!timer::interval_ticks(0xffffffffU, 0xffffffffU, ticks));
}

ASTIA_TEST(aarch64_timer_periodic_policy) {
    ASTIA_CHECK(timer::periodic_tick_budget == 5U);
    ASTIA_CHECK(timer::next_tick_action(0, timer::periodic_tick_budget) ==
                timer::tick_action::rearm);
    ASTIA_CHECK(timer::next_tick_action(3, timer::periodic_tick_budget) ==
                timer::tick_action::rearm);
    ASTIA_CHECK(timer::next_tick_action(4, timer::periodic_tick_budget) ==
                timer::tick_action::stop);
    ASTIA_CHECK(timer::next_tick_action(5, timer::periodic_tick_budget) ==
                timer::tick_action::stop);
    ASTIA_CHECK(timer::next_tick_action(0, 0) == timer::tick_action::stop);
    ASTIA_CHECK(timer::next_tick_action(6, timer::periodic_tick_budget) ==
                timer::tick_action::stop);

    u32 ticks = 0;

    ASTIA_CHECK(timer::interval_ticks(62500000U, timer::tick_interval_ms, ticks));
    ASTIA_CHECK(ticks > 0);
    ASTIA_CHECK(ticks <= timer::tval_max_positive);

    ASTIA_CHECK(!timer::valid_interval(0));
    ASTIA_CHECK(timer::valid_interval(1));
    ASTIA_CHECK(timer::valid_interval(timer::tval_max_positive));
    ASTIA_CHECK(!timer::valid_interval(timer::tval_max_positive + 1));
    ASTIA_CHECK(!timer::valid_interval(0x80000000U));
    ASTIA_CHECK(!timer::valid_interval(0xffffffffU));
}

ASTIA_TEST(aarch64_timer_timekeeping_policy) {
    ASTIA_CHECK(timer::saturating_add(1, 2) == 3U);
    ASTIA_CHECK(timer::saturating_add(~u64{0}, 1) == ~u64{0});

    timer::reset_timekeeping(1000);
    ASTIA_CHECK(!timer::periodic_active());
    ASTIA_CHECK(timer::tick_count() == 0U);
    ASTIA_CHECK(timer::delivered_ticks() == 0U);
    ASTIA_CHECK(timer::missed_ticks() == 0U);

    timer::begin_timekeeping(1000);
    ASTIA_CHECK(timer::periodic_active());

    timer::tick_progress progress = timer::account_tick(1010);

    ASTIA_CHECK(progress.delivered == 1U);
    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::delivered_ticks() == 1U);
    ASTIA_CHECK(timer::missed_ticks() == 0U);
    ASTIA_CHECK(timer::tick_count() == 1U);

    progress = timer::account_tick(1020);

    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::tick_count() == 2U);

    progress = timer::account_tick(1050);

    ASTIA_CHECK(progress.delivered == 1U);
    ASTIA_CHECK(progress.missed == 2U);
    ASTIA_CHECK(timer::delivered_ticks() == 3U);
    ASTIA_CHECK(timer::missed_ticks() == 2U);
    ASTIA_CHECK(timer::tick_count() == 5U);

    progress = timer::account_tick(1059);

    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::tick_count() == 6U);

    progress = timer::account_tick(1060);

    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::tick_count() == 7U);

    progress = timer::account_tick(1000);

    ASTIA_CHECK(progress.delivered == 1U);
    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::delivered_ticks() == 6U);
    ASTIA_CHECK(timer::tick_count() == 8U);

    timer::reset_timekeeping(0);

    progress = timer::account_tick(~u64{0});

    ASTIA_CHECK(progress.missed == ~u64{0} / timer::tick_interval_ms - 1U);
    ASTIA_CHECK(timer::tick_count() == ~u64{0} / timer::tick_interval_ms);

    progress = timer::account_tick(~u64{0});

    ASTIA_CHECK(progress.missed == 0U);
    ASTIA_CHECK(timer::delivered_ticks() == 2U);
    ASTIA_CHECK(timer::tick_count() == ~u64{0} / timer::tick_interval_ms + 1U);
}

ASTIA_TEST(aarch64_timer_deadline_policy) {
    timer::reset_deadlines();
    ASTIA_CHECK(timer::deadline_capacity == 8U);
    ASTIA_CHECK(timer::pending_deadlines() == 0U);

    u64 ms = 0;

    ASTIA_CHECK(!timer::count_to_ms(0, 0, ms));
    ASTIA_CHECK(timer::count_to_ms(625000U, 62500000U, ms));
    ASTIA_CHECK(ms == 10U);
    ASTIA_CHECK(timer::count_to_ms(0, 62500000U, ms));
    ASTIA_CHECK(ms == 0U);
    ASTIA_CHECK(timer::count_to_ms(~u64{0} / 500U, 62500000U, ms));
    ASTIA_CHECK(ms > 500000000000U);
    ASTIA_CHECK(!timer::count_to_ms(~u64{0}, 1U, ms));

    u64 due = 0;

    ASTIA_CHECK(timer::add_ms_checked(100U, 25U, due));
    ASTIA_CHECK(due == 125U);
    ASTIA_CHECK(timer::add_ms_checked(~u64{0} - 5U, 5U, due));
    ASTIA_CHECK(due == ~u64{0});
    ASTIA_CHECK(!timer::add_ms_checked(~u64{0} - 4U, 5U, due));

    ASTIA_CHECK(!timer::deadline_due(11U, 10U));
    ASTIA_CHECK(timer::deadline_due(10U, 10U));
    ASTIA_CHECK(timer::deadline_due(9U, 10U));

    ASTIA_CHECK(timer::earliest_due_index(timer::deadline_table,
                                          timer::deadline_capacity) == 0xffffffffU);

    timer::deadline_entry table[3]{};

    ASTIA_CHECK(timer::earliest_due_index(table, 3) == 0xffffffffU);

    table[1].armed  = true;
    table[1].due_ms = 50;

    ASTIA_CHECK(timer::earliest_due_index(table, 3) == 1U);

    table[0].armed  = true;
    table[0].due_ms = 70;

    ASTIA_CHECK(timer::earliest_due_index(table, 3) == 1U);

    table[2].armed  = true;
    table[2].due_ms = 50;

    ASTIA_CHECK(timer::earliest_due_index(table, 3) == 1U);

    table[1].armed = false;

    ASTIA_CHECK(timer::earliest_due_index(table, 3) == 2U);

    u32 calls = 0;
    u32 id_a  = 0;
    u32 id_b  = 0;

    ASTIA_CHECK(timer::schedule_deadline(0, 0, count_local_irq, &calls, id_a) ==
                timer::deadline_error::invalid_delay);
    ASTIA_CHECK(timer::schedule_deadline(0, 10, nullptr, nullptr, id_a) ==
                timer::deadline_error::null_handler);
    ASTIA_CHECK(timer::schedule_deadline(100, 10, count_local_irq, &calls, id_a) ==
                timer::deadline_error::none);
    ASTIA_CHECK(id_a != 0);
    ASTIA_CHECK(timer::pending_deadlines() == 1U);
    ASTIA_CHECK(timer::schedule_deadline(100, 20, count_local_irq, &calls, id_b) ==
                timer::deadline_error::none);
    ASTIA_CHECK(id_b > id_a);

    u32 filler_ids[6]{};

    for (u32 index = 0; index < 6; ++index) {
        ASTIA_CHECK(timer::schedule_deadline(1000, 10, count_local_irq, &calls,
                                             filler_ids[index]) ==
                    timer::deadline_error::none);
    }

    ASTIA_CHECK(timer::schedule_deadline(1000, 10, count_local_irq, &calls,
                                         filler_ids[0]) == timer::deadline_error::full);
    ASTIA_CHECK(timer::pending_deadlines() == timer::deadline_capacity);

    ASTIA_CHECK(timer::cancel_deadline(id_a));
    ASTIA_CHECK(!timer::cancel_deadline(id_a));
    ASTIA_CHECK(!timer::cancel_deadline(0xffffffffU));
    ASTIA_CHECK(timer::pending_deadlines() == timer::deadline_capacity - 1U);

    timer::reset_deadlines();
    ASTIA_CHECK(timer::pending_deadlines() == 0U);

    deadline_probe catch_up{};

    u32 late_id  = 0;
    u32 not_due  = 0;
    u32 extra_id = 0;

    ASTIA_CHECK(timer::schedule_deadline(0, 30, record_deadline_a, &catch_up, late_id) ==
                timer::deadline_error::none);
    ASTIA_CHECK(timer::schedule_deadline(0, 10, record_deadline_b, &catch_up, extra_id) ==
                timer::deadline_error::none);
    ASTIA_CHECK(timer::schedule_deadline(0, 50, count_local_irq, &not_due,
                                         filler_ids[0]) == timer::deadline_error::none);
    ASTIA_CHECK(timer::dispatch_due(30U) == 2U);
    ASTIA_CHECK(catch_up.calls == 2U);
    ASTIA_CHECK(catch_up.order[0] == 2U);
    ASTIA_CHECK(catch_up.order[1] == 1U);
    ASTIA_CHECK(not_due == 0U);
    ASTIA_CHECK(timer::pending_deadlines() == 1U);
    ASTIA_CHECK(timer::dispatch_due(30U) == 0U);

    timer::reset_deadlines();

    u32 fresh_id = 0;

    ASTIA_CHECK(timer::pending_deadlines() == 0U);
    ASTIA_CHECK(timer::schedule_deadline(0, 10, count_local_irq, &calls, fresh_id) ==
                timer::deadline_error::none);
    ASTIA_CHECK(fresh_id == 1U);
    ASTIA_CHECK(timer::cancel_deadline(fresh_id));
    timer::reset_deadlines();

    u32 wrapped_armed_id = 0;

    timer::deadline_next_id = 0xffffffffU;

    ASTIA_CHECK(
        timer::schedule_deadline(0, 10, count_local_irq, &calls, wrapped_armed_id) ==
        timer::deadline_error::none);
    ASTIA_CHECK(wrapped_armed_id == 0xffffffffU);

    timer::deadline_next_id = 0xffffffffU;

    u32 wrapped_new_id = 0;

    ASTIA_CHECK(timer::schedule_deadline(0, 20, count_local_irq, &calls,
                                         wrapped_new_id) == timer::deadline_error::none);
    ASTIA_CHECK(wrapped_new_id != 0U);
    ASTIA_CHECK(wrapped_new_id != wrapped_armed_id);
    ASTIA_CHECK(wrapped_new_id == 1U);
    ASTIA_CHECK(timer::pending_deadlines() == 2U);
    ASTIA_CHECK(timer::cancel_deadline(wrapped_armed_id));
    ASTIA_CHECK(!timer::cancel_deadline(wrapped_armed_id));
    ASTIA_CHECK(timer::pending_deadlines() == 1U);
    ASTIA_CHECK(timer::cancel_deadline(wrapped_new_id));
    ASTIA_CHECK(timer::pending_deadlines() == 0U);

    timer::reset_deadlines();

    deadline_reentry_probe reentry{};

    reentry.schedule_now = 10;

    u32 reentry_id = 0;

    ASTIA_CHECK(timer::schedule_deadline(0, 10, reschedule_deadline, &reentry,
                                         reentry_id) == timer::deadline_error::none);
    ASTIA_CHECK(timer::dispatch_due(10U) == 1U);
    ASTIA_CHECK(reentry.calls == 1U);
    ASTIA_CHECK(reentry.later_id != 0U);
    ASTIA_CHECK(reentry.later_calls == 0U);
    ASTIA_CHECK(timer::pending_deadlines() == 1U);
    ASTIA_CHECK(timer::dispatch_due(10U) == 0U);
    ASTIA_CHECK(timer::pending_deadlines() == 1U);
    ASTIA_CHECK(timer::dispatch_due(20U) == 1U);
    ASTIA_CHECK(reentry.later_calls == 1U);
    ASTIA_CHECK(timer::pending_deadlines() == 0U);
    timer::reset_deadlines();
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
