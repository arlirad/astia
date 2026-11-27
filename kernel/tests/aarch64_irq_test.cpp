#include "irq.hpp"
#include "test.hpp"

namespace {
    using namespace astia;
    namespace irq = astia::arch::aarch64::irq;

    u32 handoff_calls = 0;

    void count_route(void* context) {
        ++*static_cast<u32*>(context);
    }

    void count_handoff() {
        ++handoff_calls;
    }

    struct lifecycle_probe {
        irq::route_error   unregister_self  = irq::route_error::none;
        irq::route_error   unregister_other = irq::route_error::none;
        irq::handoff_error set_handoff      = irq::handoff_error::none;
        irq::handoff_error clear_handoff    = irq::handoff_error::none;
        u32                calls            = 0;
    };

    void probe_lifecycle(void* context) {
        auto* probe = static_cast<lifecycle_probe*>(context);

        ++probe->calls;

        probe->unregister_self  = irq::unregister_route(6);
        probe->unregister_other = irq::unregister_route(7);
        probe->set_handoff      = irq::set_handoff(count_handoff);
        probe->clear_handoff    = irq::clear_handoff();
    }

    struct handoff_reentry_probe {
        bool ran = false;
    };

    void attempt_handoff(void* context) {
        auto* probe = static_cast<handoff_reentry_probe*>(context);

        probe->ran = irq::run_handoff();
    }
} // namespace

ASTIA_TEST(aarch64_irq_intid_partition) {
    ASTIA_CHECK(irq::classify_intid(0) == irq::intid_class::sgi);
    ASTIA_CHECK(irq::classify_intid(irq::max_sgi_id) == irq::intid_class::sgi);
    ASTIA_CHECK(irq::classify_intid(irq::first_ppi_id) == irq::intid_class::ppi);
    ASTIA_CHECK(irq::classify_intid(irq::max_local_intid) == irq::intid_class::ppi);
    ASTIA_CHECK(irq::classify_intid(irq::first_spi_id) == irq::intid_class::spi);
    ASTIA_CHECK(irq::classify_intid(irq::last_spi_id) == irq::intid_class::spi);
    ASTIA_CHECK(irq::classify_intid(irq::special_intid_first) ==
                irq::intid_class::special);
    ASTIA_CHECK(irq::classify_intid(irq::spurious_intid) == irq::intid_class::special);
    ASTIA_CHECK(irq::classify_intid(irq::special_intid_last + 1) ==
                irq::intid_class::invalid);
    ASTIA_CHECK(irq::classify_intid(irq::intid_mask) == irq::intid_class::invalid);

    ASTIA_CHECK(irq::local_intid_limit == 32U);
    ASTIA_CHECK(irq::valid_local_intid(0));
    ASTIA_CHECK(irq::valid_local_intid(irq::max_local_intid));
    ASTIA_CHECK(!irq::valid_local_intid(irq::first_spi_id));
    ASTIA_CHECK(!irq::valid_local_intid(irq::spurious_intid));
}

ASTIA_TEST(aarch64_irq_registration_lifecycle) {
    irq::reset();

    u32 calls = 0;

    ASTIA_CHECK(irq::register_route(irq::first_spi_id, count_route, &calls) ==
                irq::route_error::invalid_intid);
    ASTIA_CHECK(irq::register_route(irq::spurious_intid, count_route, &calls) ==
                irq::route_error::invalid_intid);
    ASTIA_CHECK(irq::register_route(6, nullptr, nullptr) ==
                irq::route_error::null_handler);
    ASTIA_CHECK(!irq::registered(6));
    ASTIA_CHECK(irq::register_route(6, count_route, &calls) == irq::route_error::none);
    ASTIA_CHECK(irq::registered(6));
    ASTIA_CHECK(irq::register_route(6, count_route, &calls) ==
                irq::route_error::already_registered);
    ASTIA_CHECK(irq::unregister_route(6) == irq::route_error::none);
    ASTIA_CHECK(!irq::registered(6));
    ASTIA_CHECK(irq::unregister_route(6) == irq::route_error::not_registered);
    ASTIA_CHECK(irq::unregister_route(irq::first_spi_id) ==
                irq::route_error::invalid_intid);
    ASTIA_CHECK(irq::register_route(6, count_route, &calls) == irq::route_error::none);
    ASTIA_CHECK(irq::unregister_route(6) == irq::route_error::none);
}

ASTIA_TEST(aarch64_irq_dispatch_policy) {
    irq::reset();

    u32 calls = 0;

    irq::dispatch_result unregistered = irq::dispatch(6);

    ASTIA_CHECK(unregistered.action == irq::dispatch_action::fatal);
    ASTIA_CHECK(unregistered.intid == 6U);
    ASTIA_CHECK(unregistered.owner.callback == nullptr);
    ASTIA_CHECK(irq::stats().dispatches == 1U);
    ASTIA_CHECK(irq::stats().fatal == 1U);

    ASTIA_CHECK(irq::register_route(6, count_route, &calls) == irq::route_error::none);

    irq::dispatch_result handled = irq::dispatch(0x80000006U);

    ASTIA_CHECK(handled.action == irq::dispatch_action::handled);
    ASTIA_CHECK(handled.intid == 6U);
    ASTIA_CHECK(handled.iar == 0x80000006U);
    ASTIA_CHECK(handled.owner.callback == count_route);
    ASTIA_CHECK(handled.owner.context == &calls);
    ASTIA_CHECK(handled.owner.intid == 6U);
    ASTIA_CHECK(calls == 1U);
    ASTIA_CHECK(irq::stats().dispatches == 2U);
    ASTIA_CHECK(irq::stats().handled == 1U);

    ASTIA_CHECK(irq::dispatch(irq::spurious_intid).action ==
                irq::dispatch_action::spurious);
    ASTIA_CHECK(irq::dispatch(0x800003ffU).action == irq::dispatch_action::spurious);
    ASTIA_CHECK(irq::dispatch(irq::first_spi_id).action == irq::dispatch_action::fatal);
    ASTIA_CHECK(irq::dispatch(irq::special_intid_first).action ==
                irq::dispatch_action::fatal);
    ASTIA_CHECK(irq::dispatch(irq::special_intid_last - 1).action ==
                irq::dispatch_action::fatal);
    ASTIA_CHECK(irq::dispatch(irq::intid_mask).action == irq::dispatch_action::fatal);
    ASTIA_CHECK(calls == 1U);
    ASTIA_CHECK(irq::stats().handled == 1U);
    ASTIA_CHECK(irq::stats().spurious == 2U);
    ASTIA_CHECK(irq::stats().fatal == 5U);
}

ASTIA_TEST(aarch64_irq_inflight_ownership) {
    irq::reset();

    lifecycle_probe probe{};

    u32 other_calls = 0;

    ASTIA_CHECK(irq::register_route(6, probe_lifecycle, &probe) ==
                irq::route_error::none);
    ASTIA_CHECK(irq::register_route(7, count_route, &other_calls) ==
                irq::route_error::none);
    ASTIA_CHECK(irq::set_handoff(count_handoff) == irq::handoff_error::none);

    const irq::dispatch_result result = irq::dispatch(6);

    ASTIA_CHECK(result.action == irq::dispatch_action::handled);
    ASTIA_CHECK(probe.calls == 1U);
    ASTIA_CHECK(probe.unregister_self == irq::route_error::busy);
    ASTIA_CHECK(probe.unregister_other == irq::route_error::busy);
    ASTIA_CHECK(probe.set_handoff == irq::handoff_error::busy);
    ASTIA_CHECK(probe.clear_handoff == irq::handoff_error::busy);
    ASTIA_CHECK(irq::registered(6));
    ASTIA_CHECK(irq::registered(7));

    ASTIA_CHECK(irq::dispatch(7).action == irq::dispatch_action::handled);
    ASTIA_CHECK(other_calls == 1U);
    ASTIA_CHECK(irq::unregister_route(7) == irq::route_error::none);
    ASTIA_CHECK(irq::unregister_route(6) == irq::route_error::none);
}

ASTIA_TEST(aarch64_irq_handoff_boundary) {
    irq::reset();

    handoff_calls = 0;

    ASTIA_CHECK(irq::set_handoff(nullptr) == irq::handoff_error::null_callback);
    ASTIA_CHECK(irq::set_handoff(count_handoff) == irq::handoff_error::none);
    ASTIA_CHECK(irq::set_handoff(count_handoff) ==
                irq::handoff_error::already_registered);
    ASTIA_CHECK(irq::run_handoff());
    ASTIA_CHECK(handoff_calls == 1U);
    ASTIA_CHECK(irq::stats().handoffs == 1U);
    ASTIA_CHECK(irq::clear_handoff() == irq::handoff_error::none);
    ASTIA_CHECK(!irq::run_handoff());
    ASTIA_CHECK(handoff_calls == 1U);
    ASTIA_CHECK(irq::clear_handoff() == irq::handoff_error::not_registered);
}

ASTIA_TEST(aarch64_irq_handoff_not_from_handler) {
    irq::reset();

    handoff_calls = 0;

    handoff_reentry_probe probe{};

    ASTIA_CHECK(irq::set_handoff(count_handoff) == irq::handoff_error::none);
    ASTIA_CHECK(irq::register_route(6, attempt_handoff, &probe) ==
                irq::route_error::none);

    const irq::dispatch_result result = irq::dispatch(6);

    ASTIA_CHECK(result.action == irq::dispatch_action::handled);
    ASTIA_CHECK(!probe.ran);
    ASTIA_CHECK(handoff_calls == 0U);
    ASTIA_CHECK(irq::stats().handoffs == 0U);
    ASTIA_CHECK(irq::run_handoff());
    ASTIA_CHECK(handoff_calls == 1U);
    ASTIA_CHECK(irq::stats().handoffs == 1U);
}

ASTIA_TEST(aarch64_irq_reset) {
    irq::reset();

    u32 calls = 0;

    ASTIA_CHECK(irq::register_route(6, count_route, &calls) == irq::route_error::none);
    ASTIA_CHECK(irq::set_handoff(count_handoff) == irq::handoff_error::none);
    ASTIA_CHECK(irq::dispatch(6).action == irq::dispatch_action::handled);

    irq::reset();

    ASTIA_CHECK(!irq::registered(6));
    ASTIA_CHECK(irq::stats().dispatches == 0U);
    ASTIA_CHECK(irq::stats().handled == 0U);
    ASTIA_CHECK(irq::stats().handoffs == 0U);
    ASTIA_CHECK(!irq::run_handoff());
    ASTIA_CHECK(irq::dispatch(6).action == irq::dispatch_action::fatal);
}
