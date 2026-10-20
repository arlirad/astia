#include "support/test.hpp"
#include <astia/signal.hpp>
#include <astia/api/signal.h>

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

ASTIA_TEST(signal_disposition_preserves_handler_mask_and_restorer) {
    astia::signal_state state;
    const auto signal = astia::signal_number::usr1;
    const auto handler = reinterpret_cast<astia::signal_handler>(0x400123);
    const astia::signal_disposition installed{
        astia::signal_action::handler, handler, 0x1357u, 0x2468u};

    ASTIA_CHECK(state.disposition(signal, installed));
    const auto observed = state.disposition(signal);
    ASTIA_CHECK(observed.action == astia::signal_action::handler);
    ASTIA_CHECK(observed.handler == handler);
    ASTIA_CHECK(observed.mask == 0x2468u);
    ASTIA_CHECK(observed.restorer == 0x1357u);
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

ASTIA_TEST(x86_64_signal_abi_keeps_register_order_and_frame_size) {
    ASTIA_CHECK(sizeof(astia_x86_64_signal_context_t) == 152);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rip) == 120);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rsp) == 128);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rflags) == 136);
    ASTIA_CHECK(sizeof(astia_sigaction_t) == 24);
}
