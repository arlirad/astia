#include <astia/arch/interrupts.hpp>

#include "support/interrupts_host.hpp"
#include "support/test.hpp"

namespace {
    unsigned handler_calls       = 0;
    unsigned enter_calls         = 0;
    unsigned leave_calls         = 0;
    unsigned order               = 0;
    unsigned enter_order         = 0;
    unsigned handler_order       = 0;
    unsigned leave_order         = 0;
    unsigned eoi_order           = 0;
    unsigned eoi_calls           = 0;
    bool     unregister_in_enter = false;

    void reset() {
        handler_calls = 0;
        enter_calls   = 0;
        leave_calls   = 0;
        order         = 0;
        enter_order   = 0;
        handler_order = 0;
        leave_order   = 0;
        eoi_order     = 0;
        eoi_calls     = 0;
    }

    void observe_eoi(astia::u8) {
        ++eoi_calls;

        eoi_order = ++order;
    }

    void handler() {
        ++handler_calls;

        handler_order = ++order;
    }

    bool refuse(void*) {
        ++enter_calls;

        return false;
    }

    bool accept(void*) {
        ++enter_calls;

        enter_order = ++order;

        return true;
    }

    void leave(void*) {
        ++leave_calls;

        leave_order = ++order;
    }

    bool refuse_and_unregister(void*) {
        unregister_in_enter = astia::arch::unregister_irq_handler(6);

        return true;
    }
} // namespace

ASTIA_TEST(interrupts_guard_enter_refusal_skips_handler_and_leave) {
    reset();
    astia::arch::host_test::set_eoi_observer(observe_eoi);

    ASTIA_CHECK(astia::arch::register_irq_handler(5, handler, refuse, leave, nullptr));
    astia::arch::host_test::dispatch_irq(5);

    ASTIA_CHECK(handler_calls == 0);
    ASTIA_CHECK(enter_calls == 1);
    ASTIA_CHECK(leave_calls == 0);
    ASTIA_CHECK(eoi_calls == 1);
    ASTIA_CHECK(astia::arch::unregister_irq_handler(5));
    astia::arch::host_test::set_eoi_observer(nullptr);
}

ASTIA_TEST(interrupts_guard_enter_orders_handler_before_leave) {
    reset();
    astia::arch::host_test::set_eoi_observer(observe_eoi);

    ASTIA_CHECK(astia::arch::register_irq_handler(6, handler, accept, leave, nullptr));
    astia::arch::host_test::dispatch_irq(6);

    ASTIA_CHECK(handler_calls == 1);
    ASTIA_CHECK(enter_calls == 1);
    ASTIA_CHECK(leave_calls == 1);
    ASTIA_CHECK(enter_order == 1);
    ASTIA_CHECK(handler_order == 2);
    ASTIA_CHECK(leave_order == 3);
    ASTIA_CHECK(eoi_order == 4);
    ASTIA_CHECK(astia::arch::unregister_irq_handler(6));
    astia::arch::host_test::set_eoi_observer(nullptr);
}

ASTIA_TEST(interrupts_guard_eoi_is_unconditional_after_leave) {
    reset();
    astia::arch::host_test::set_eoi_observer(observe_eoi);

    ASTIA_CHECK(astia::arch::register_irq_handler(9, handler, accept, leave, nullptr));
    ASTIA_CHECK(astia::arch::mask_irq_handler(9));
    astia::arch::host_test::dispatch_irq(9);

    ASTIA_CHECK(handler_calls == 0);
    ASTIA_CHECK(enter_calls == 0);
    ASTIA_CHECK(leave_calls == 0);
    ASTIA_CHECK(eoi_calls == 1);

    ASTIA_CHECK(astia::arch::unregister_irq_handler(9));
    astia::arch::host_test::set_eoi_observer(nullptr);
}

ASTIA_TEST(interrupts_guard_rejects_half_bound_pairs) {
    reset();

    ASTIA_CHECK(!astia::arch::register_irq_handler(7, handler, accept, nullptr, nullptr));
    ASTIA_CHECK(!astia::arch::register_irq_handler(7, handler, nullptr, leave, nullptr));
    ASTIA_CHECK(!astia::arch::register_irq_handler(7, nullptr, accept, leave, nullptr));
    ASTIA_CHECK(astia::arch::register_irq_handler(7, handler, nullptr, nullptr, nullptr));
    ASTIA_CHECK(
        !astia::arch::register_irq_handler(7, handler, nullptr, nullptr, nullptr));
    ASTIA_CHECK(astia::arch::unregister_irq_handler(7));
}

ASTIA_TEST(interrupts_guard_mask_keeps_claim_but_drops_dispatch) {
    reset();

    ASTIA_CHECK(astia::arch::register_irq_handler(8, handler, nullptr, nullptr, nullptr));
    ASTIA_CHECK(astia::arch::mask_irq_handler(8));
    astia::arch::host_test::dispatch_irq(8);

    ASTIA_CHECK(handler_calls == 0);
    ASTIA_CHECK(
        !astia::arch::register_irq_handler(8, handler, nullptr, nullptr, nullptr));

    ASTIA_CHECK(astia::arch::unregister_irq_handler(8));
    astia::arch::host_test::dispatch_irq(8);

    ASTIA_CHECK(handler_calls == 0);
}

ASTIA_TEST(interrupts_guard_unregister_refuses_while_in_flight) {
    reset();

    unregister_in_enter = true;

    ASTIA_CHECK(astia::arch::register_irq_handler(6, handler, refuse_and_unregister,
                                                  leave, nullptr));
    astia::arch::host_test::dispatch_irq(6);

    ASTIA_CHECK(!unregister_in_enter);
    ASTIA_CHECK(handler_calls == 1);
    ASTIA_CHECK(leave_calls == 1);
    ASTIA_CHECK(astia::arch::unregister_irq_handler(6));

    astia::arch::host_test::dispatch_irq(6);

    ASTIA_CHECK(handler_calls == 1);
}
