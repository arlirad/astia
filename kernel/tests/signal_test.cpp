#include "support/test.hpp"
#include <astia/signal.hpp>

ASTIA_TEST(signal_state_queues_and_masks_signals) {
    astia::signal_state state;
    ASTIA_CHECK(state.send(astia::signal_number::int_));
    ASTIA_CHECK(state.send(astia::signal_number::term));
    state.block(astia::signal_bit(astia::signal_number::int_));
    ASTIA_CHECK(state.take_unblocked() == astia::signal_number::term);
    ASTIA_CHECK(state.take_unblocked() == static_cast<astia::signal_number>(0));
    state.unblock(astia::signal_bit(astia::signal_number::int_));
    ASTIA_CHECK(state.take_unblocked() == astia::signal_number::int_);
}

ASTIA_TEST(signal_state_protects_uncatchable_signals) {
    astia::signal_state state;
    ASTIA_CHECK(!state.disposition(astia::signal_number::kill,
                                   {astia::signal_action::ignore, nullptr}));
    ASTIA_CHECK(!state.disposition(astia::signal_number::stop,
                                   {astia::signal_action::handler, nullptr}));
    state.block(~astia::signal_mask{});
    ASTIA_CHECK((state.blocked() & astia::signal_bit(astia::signal_number::kill)) == 0);
    ASTIA_CHECK((state.blocked() & astia::signal_bit(astia::signal_number::stop)) == 0);
}

ASTIA_TEST(signal_default_actions_match_posix_categories) {
    ASTIA_CHECK(astia::default_action(astia::signal_number::term) ==
                astia::signal_default::terminate);
    ASTIA_CHECK(astia::default_action(astia::signal_number::segv) ==
                astia::signal_default::core);
    ASTIA_CHECK(astia::default_action(astia::signal_number::stop) ==
                astia::signal_default::stop);
    ASTIA_CHECK(astia::default_action(astia::signal_number::chld) ==
                astia::signal_default::ignore);
}
