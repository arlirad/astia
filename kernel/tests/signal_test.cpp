#include <astia/api/signal.h>

#include "../src/proc/signal_info.hpp"
#include "../src/proc/signal_permission.hpp"
#include "../src/proc/signal_waiter_registry.hpp"
#include "support/test.hpp"

#include <signal.hpp>

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

ASTIA_TEST(signal_state_coalesces_repeated_sends_to_the_last_payload) {
    astia::signal_state state;
    astia::signal_info  first{0, 0, ASTIA_SI_USER, 10, 1, 0, 0};
    astia::signal_info  second{0, 0, ASTIA_SI_USER, 20, 2, 0, 0};
    astia::signal_info  observed{};

    ASTIA_CHECK(state.send(astia::signal_number::usr1, first));
    ASTIA_CHECK(state.send(astia::signal_number::usr1, second));
    ASTIA_CHECK(state.pending() == astia::signal_bit(astia::signal_number::usr1));

    ASTIA_CHECK(state.take_unblocked(nullptr, &observed) == astia::signal_number::usr1);
    ASTIA_CHECK(observed.pid == second.pid);
    ASTIA_CHECK(observed.uid == second.uid);
    ASTIA_CHECK(state.pending() == 0);
    ASTIA_CHECK(state.take_unblocked() == static_cast<astia::signal_number>(0));
}

ASTIA_TEST(signal_state_selects_the_lowest_pending_number_first) {
    astia::signal_state state;

    ASTIA_CHECK(state.send(astia::signal_number::usr2));
    ASTIA_CHECK(state.send(astia::signal_number::usr1));

    ASTIA_CHECK(state.take_unblocked() == astia::signal_number::usr1);
    ASTIA_CHECK(state.take_unblocked() == astia::signal_number::usr2);
    ASTIA_CHECK(state.take_unblocked() == static_cast<astia::signal_number>(0));
}

ASTIA_TEST(signal_state_reports_only_unblocked_fatal_defaults) {
    astia::signal_state state;

    ASTIA_CHECK(state.pending_fatal_default_for(0) ==
                static_cast<astia::signal_number>(0));

    ASTIA_CHECK(state.send(astia::signal_number::chld));
    ASTIA_CHECK(state.pending_fatal_default_for(0) ==
                static_cast<astia::signal_number>(0));

    ASTIA_CHECK(state.send(astia::signal_number::term));
    ASTIA_CHECK(state.pending_fatal_default_for(0) == astia::signal_number::term);
    ASTIA_CHECK(state.pending_fatal_default_for(astia::signal_bit(
                    astia::signal_number::term)) == static_cast<astia::signal_number>(0));

    const auto handler = reinterpret_cast<astia::signal_handler>(0x400123);

    ASTIA_CHECK(state.disposition(astia::signal_number::term,
                                  {astia::signal_action::handler, handler}));
    ASTIA_CHECK(state.pending_fatal_default_for(0) ==
                static_cast<astia::signal_number>(0));

    ASTIA_CHECK(state.send(astia::signal_number::segv));
    ASTIA_CHECK(state.pending_fatal_default_for(0) == astia::signal_number::segv);

    ASTIA_CHECK(state.disposition(astia::signal_number::segv,
                                  {astia::signal_action::ignore, nullptr}));
    ASTIA_CHECK(state.pending_fatal_default_for(0) ==
                static_cast<astia::signal_number>(0));
}

ASTIA_TEST(signal_state_sigwait_consumes_only_matching_signal) {
    astia::signal_state state;

    ASTIA_CHECK(state.send(astia::signal_number::int_));
    ASTIA_CHECK(state.send(astia::signal_number::usr1));
    ASTIA_CHECK(state.take_matching(astia::signal_bit(astia::signal_number::usr1)) ==
                astia::signal_number::usr1);
    ASTIA_CHECK(state.pending() == astia::signal_bit(astia::signal_number::int_));
    ASTIA_CHECK(state.take_matching(astia::signal_bit(astia::signal_number::usr1)) ==
                static_cast<astia::signal_number>(0));
}

ASTIA_TEST(signal_state_fork_inherits_dispositions_without_pending) {
    astia::signal_state parent;
    astia::signal_state child;
    const auto          handler = reinterpret_cast<astia::signal_handler>(0x400123);

    ASTIA_CHECK(parent.disposition(astia::signal_number::usr1,
                                   {astia::signal_action::handler, handler, 0x12, 0x34}));
    ASTIA_CHECK(parent.disposition(astia::signal_number::usr2,
                                   {astia::signal_action::ignore, nullptr}));
    ASTIA_CHECK(parent.send(astia::signal_number::usr1));
    child.inherit_dispositions_from(parent);

    const auto inherited_handler = child.disposition(astia::signal_number::usr1);

    ASTIA_CHECK(inherited_handler.action == astia::signal_action::handler);
    ASTIA_CHECK(inherited_handler.handler == handler);
    ASTIA_CHECK(inherited_handler.restorer == 0x12);
    ASTIA_CHECK(inherited_handler.mask == 0x34);
    ASTIA_CHECK(child.disposition(astia::signal_number::usr2).action ==
                astia::signal_action::ignore);
    ASTIA_CHECK(child.pending() == 0);
}

ASTIA_TEST(signal_state_exec_resets_caught_and_preserves_ignored) {
    astia::signal_state state;
    const auto          handler = reinterpret_cast<astia::signal_handler>(0x400123);

    ASTIA_CHECK(state.disposition(astia::signal_number::usr1,
                                  {astia::signal_action::handler, handler}));
    ASTIA_CHECK(state.disposition(astia::signal_number::usr2,
                                  {astia::signal_action::ignore, nullptr}));
    state.reset_caught_for_exec();

    ASTIA_CHECK(state.disposition(astia::signal_number::usr1).action ==
                astia::signal_action::default_action);
    ASTIA_CHECK(state.disposition(astia::signal_number::usr2).action ==
                astia::signal_action::ignore);
}

ASTIA_TEST(signal_state_repeated_wait_preserves_unmatched_signal) {
    astia::signal_state state;
    const auto          first  = astia::signal_bit(astia::signal_number::usr1);
    const auto          second = astia::signal_bit(astia::signal_number::usr2);

    ASTIA_CHECK(state.send(astia::signal_number::usr1));
    ASTIA_CHECK(state.send(astia::signal_number::usr2));
    ASTIA_CHECK(state.take_matching(second) == astia::signal_number::usr2);
    ASTIA_CHECK(state.pending() == first);
    ASTIA_CHECK(state.take_matching(second) == static_cast<astia::signal_number>(0));
    ASTIA_CHECK(state.take_matching(first) == astia::signal_number::usr1);
    ASTIA_CHECK(state.pending() == 0);
}

ASTIA_TEST(signal_state_preserves_user_signal_metadata) {
    astia::signal_state state;
    astia::signal_info  sent{static_cast<astia::i32>(astia::signal_number::usr1),
                            0,
                            ASTIA_SI_USER,
                            123,
                            456,
                            0};
    astia::signal_info  observed{};

    ASTIA_CHECK(state.send(astia::signal_number::usr1, sent));
    ASTIA_CHECK(state.take_unblocked(nullptr, &observed) == astia::signal_number::usr1);
    ASTIA_CHECK(observed.signo == sent.signo);
    ASTIA_CHECK(observed.code == ASTIA_SI_USER);
    ASTIA_CHECK(observed.pid == 123);
    ASTIA_CHECK(observed.uid == 456);
}

ASTIA_TEST(signal_waiters_remain_registered_until_removed) {
    astia::proc::signal_waiter_registry waiters;
    auto*                               first = reinterpret_cast<astia::proc::thread*>(1);
    auto*                second               = reinterpret_cast<astia::proc::thread*>(2);
    astia::proc::thread* observed[2]{};

    ASTIA_CHECK(waiters.add(first));
    ASTIA_CHECK(waiters.add(first));
    ASTIA_CHECK(waiters.snapshot(observed, 2) == 1);
    ASTIA_CHECK(observed[0] == first);
    ASTIA_CHECK(waiters.add(second));
    ASTIA_CHECK(waiters.snapshot(observed, 2) == 2);
    waiters.remove(first);
    ASTIA_CHECK(waiters.snapshot(observed, 2) == 1);
    ASTIA_CHECK(observed[0] == second);
}

ASTIA_TEST(signal_waiters_expire_only_timed_entries) {
    astia::proc::signal_waiter_registry waiters;
    auto*                               timed = reinterpret_cast<astia::proc::thread*>(1);
    auto*                               plain = reinterpret_cast<astia::proc::thread*>(2);
    astia::proc::thread*                expired[2]{};

    ASTIA_CHECK(waiters.add(timed, true, 10));
    ASTIA_CHECK(waiters.add(plain));
    ASTIA_CHECK(waiters.expire(9, expired, 2) == 0);
    ASTIA_CHECK(waiters.expire(10, expired, 2) == 1);
    ASTIA_CHECK(expired[0] == timed);
    ASTIA_CHECK(waiters.timed_out(timed));
    ASTIA_CHECK(waiters.expire(11, expired, 2) == 0);
    ASTIA_CHECK(!waiters.timed_out(plain));
}

ASTIA_TEST(signal_waiters_publish_wakes_before_blocking) {
    astia::proc::signal_waiter_registry waiters;
    auto*                               first = reinterpret_cast<astia::proc::thread*>(1);
    astia::proc::thread*                wake_list[1]{};

    ASTIA_CHECK(waiters.add(first));
    ASTIA_CHECK(waiters.wake(wake_list, 1) == 1);
    ASTIA_CHECK(wake_list[0] == first);
    ASTIA_CHECK(waiters.consume_wake(first));
    ASTIA_CHECK(!waiters.consume_wake(first));
}

ASTIA_TEST(signal_state_sigwait_restores_mask_and_preserves_pending_signal) {
    astia::signal_state state;
    const auto          prior  = astia::signal_bit(astia::signal_number::term);
    const auto          waited = astia::signal_bit(astia::signal_number::usr1);

    state.set_blocked(prior);
    ASTIA_CHECK(state.send(astia::signal_number::int_));
    ASTIA_CHECK(state.send(astia::signal_number::usr1));
    state.block(waited);
    ASTIA_CHECK(state.blocked() == (prior | waited));
    ASTIA_CHECK(state.take_matching(waited) == astia::signal_number::usr1);
    ASTIA_CHECK(state.pending() == astia::signal_bit(astia::signal_number::int_));
    state.set_blocked(prior);
    ASTIA_CHECK(state.blocked() == prior);
    ASTIA_CHECK(state.take_unblocked() == astia::signal_number::int_);
}

ASTIA_TEST(signal_state_default_sigchld_does_not_interrupt_waiters) {
    astia::signal_state state;

    ASTIA_CHECK(state.send(astia::signal_number::chld));
    ASTIA_CHECK(!state.has_unblocked_interrupting());
    ASTIA_CHECK(state.send(astia::signal_number::alrm));
    ASTIA_CHECK(state.has_unblocked_interrupting());
}

ASTIA_TEST(signal_state_alarm_respects_ignore_and_block_mask) {
    astia::signal_state state;

    ASTIA_CHECK(state.disposition(astia::signal_number::alrm,
                                  {astia::signal_action::ignore, nullptr}));
    ASTIA_CHECK(state.send(astia::signal_number::alrm));
    ASTIA_CHECK(!state.has_unblocked_interrupting());

    astia::signal_state blocked;

    blocked.block(astia::signal_bit(astia::signal_number::alrm));
    ASTIA_CHECK(blocked.send(astia::signal_number::alrm));
    ASTIA_CHECK(!blocked.has_unblocked_interrupting());
    blocked.unblock(astia::signal_bit(astia::signal_number::alrm));
    ASTIA_CHECK(blocked.has_unblocked_interrupting());
}

ASTIA_TEST(signal_permission_matches_process_credentials) {
    ASTIA_CHECK(astia::proc::signal_permission(1, 1, 0, 0) == false);
    ASTIA_CHECK(astia::proc::signal_permission(1, 1, 1, 0));
    ASTIA_CHECK(astia::proc::signal_permission(1, 1, 0, 1));
    ASTIA_CHECK(astia::proc::signal_permission(0, 1, 0, 1));
    ASTIA_CHECK(astia::proc::signal_permission(1, 0, 0, 0));
}

ASTIA_TEST(signal_all_skips_init_and_caller_but_reaches_siblings) {
    using astia::proc::signal_all_target_eligible;

    const auto caller  = astia::pid_from_raw(7);
    const auto init    = astia::pid_from_raw(1);
    const auto sibling = astia::pid_from_raw(8);

    ASTIA_CHECK(!signal_all_target_eligible(true, init, caller));
    ASTIA_CHECK(!signal_all_target_eligible(true, caller, caller));
    ASTIA_CHECK(signal_all_target_eligible(true, sibling, caller));
    ASTIA_CHECK(!signal_all_target_eligible(false, sibling, caller));
}

ASTIA_TEST(signal_explicit_pid_one_keeps_permission_result) {
    ASTIA_CHECK(astia::proc::signal_permission(0, 0, 0, 0));
    ASTIA_CHECK(!astia::proc::signal_permission(1, 1, 0, 0));
}

ASTIA_TEST(signal_audience_filters_group_members) {
    using astia::proc::signal_audience;
    using astia::proc::signal_audience_status;

    signal_audience empty;

    ASTIA_CHECK(empty.status() == signal_audience_status::none);

    signal_audience denied;

    ASTIA_CHECK(!denied.admit(1, 1, 0, 0));
    ASTIA_CHECK(denied.status() == signal_audience_status::denied);

    signal_audience mixed;

    ASTIA_CHECK(mixed.admit(1, 1, 1, 1));
    ASTIA_CHECK(!mixed.admit(1, 1, 0, 0));
    ASTIA_CHECK(mixed.status() == signal_audience_status::allowed);

    signal_audience root;

    ASTIA_CHECK(root.admit(0, 0, 1, 1));
    ASTIA_CHECK(root.admit(0, 0, 0, 0));
    ASTIA_CHECK(root.status() == signal_audience_status::allowed);
}

ASTIA_TEST(signal_numbers_are_unique_and_sigaio_occupies_id16) {
    constexpr astia::signal_number identifiers[] = {
        astia::signal_number::hup,  astia::signal_number::int_,
        astia::signal_number::quit, astia::signal_number::ill,
        astia::signal_number::trap, astia::signal_number::abrt,
        astia::signal_number::bus,  astia::signal_number::fpe,
        astia::signal_number::kill, astia::signal_number::usr1,
        astia::signal_number::segv, astia::signal_number::usr2,
        astia::signal_number::pipe, astia::signal_number::alrm,
        astia::signal_number::term, astia::signal_number::aio,
        astia::signal_number::chld, astia::signal_number::cont,
        astia::signal_number::stop, astia::signal_number::tstp,
        astia::signal_number::ttin, astia::signal_number::ttou,
        astia::signal_number::urg,  astia::signal_number::xcpu,
        astia::signal_number::xfsz, astia::signal_number::vtalrm,
        astia::signal_number::prof, astia::signal_number::winch,
        astia::signal_number::io,   astia::signal_number::pwr,
        astia::signal_number::sys,
    };

    bool assigned[astia::signal_count]{};

    for (const auto identifier : identifiers) {
        const auto value = static_cast<astia::u8>(identifier);

        ASTIA_CHECK(value > 0 && value < astia::signal_count);
        ASTIA_CHECK(!assigned[value]);

        assigned[value] = true;
    }

    ASTIA_CHECK(assigned[16]);
    ASTIA_CHECK(!assigned[0]);
}

ASTIA_TEST(signal_sigaio_is_an_ordinary_signal_with_id16) {
    ASTIA_CHECK(static_cast<astia::u8>(astia::signal_number::aio) == 16);
    ASTIA_CHECK(static_cast<astia::u8>(astia::signal_number::io) == 29);
    ASTIA_CHECK(astia::signal_count == 32);
    ASTIA_CHECK(astia::valid_signal(astia::signal_number::aio));
    ASTIA_CHECK(astia::valid_signal(astia::signal_number::io));
    ASTIA_CHECK(astia::signal_bit(astia::signal_number::aio) ==
                (astia::signal_mask{1} << 16));
    ASTIA_CHECK(!astia::is_uncatchable(astia::signal_number::aio));
    ASTIA_CHECK(astia::default_action(astia::signal_number::aio) ==
                astia::signal_default::ignore);
    ASTIA_CHECK(astia::default_action(astia::signal_number::io) ==
                astia::signal_default::ignore);
}

ASTIA_TEST(signal_state_catches_and_masks_sigaio) {
    astia::signal_state state;
    const auto          handler = reinterpret_cast<astia::signal_handler>(0x400123);

    ASTIA_CHECK(state.disposition(astia::signal_number::aio,
                                  {astia::signal_action::handler, handler}));
    ASTIA_CHECK(state.disposition(astia::signal_number::io,
                                  {astia::signal_action::ignore, nullptr}));
    state.block(astia::signal_bit(astia::signal_number::aio));
    ASTIA_CHECK((state.blocked() & astia::signal_bit(astia::signal_number::aio)) != 0);
    ASTIA_CHECK(state.send(astia::signal_number::aio));
    ASTIA_CHECK(state.take_unblocked() == static_cast<astia::signal_number>(0));
    ASTIA_CHECK(state.take_matching(astia::signal_bit(astia::signal_number::aio)) ==
                astia::signal_number::aio);
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
    const auto          signal  = astia::signal_number::usr1;
    const auto          handler = reinterpret_cast<astia::signal_handler>(0x400123);
    const astia::signal_disposition installed{astia::signal_action::handler, handler,
                                              0x1357u, 0x2468u};

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

ASTIA_TEST(signal_info_initialization_covers_full_abi_storage) {
    const auto info = astia::proc::make_siginfo(11, 0, 1, 42, 1000, 7, 0x1234);

    ASTIA_CHECK(info.signo == 11);
    ASTIA_CHECK(info.errno_value == 0);
    ASTIA_CHECK(info.code == 1);
    ASTIA_CHECK(info.pid == 42);
    ASTIA_CHECK(info.uid == 1000);
    ASTIA_CHECK(info.status == 7);
    ASTIA_CHECK(info.addr == 0x1234);

    const auto* bytes = reinterpret_cast<const astia::u8*>(&info);

    bool covered[sizeof(astia_siginfo_t)]{};

    const auto cover = [&covered](astia::usz offset, astia::usz length) {
        for (astia::usz i = 0; i < length; ++i) {
            covered[offset + i] = true;
        }
    };

    cover(offsetof(astia_siginfo_t, signo), sizeof(info.signo));
    cover(offsetof(astia_siginfo_t, errno_value), sizeof(info.errno_value));
    cover(offsetof(astia_siginfo_t, code), sizeof(info.code));
    cover(offsetof(astia_siginfo_t, pid), sizeof(info.pid));
    cover(offsetof(astia_siginfo_t, uid), sizeof(info.uid));
    cover(offsetof(astia_siginfo_t, status), sizeof(info.status));
    cover(offsetof(astia_siginfo_t, addr), sizeof(info.addr));

    for (astia::usz i = 0; i < sizeof(astia_siginfo_t); ++i) {
        ASTIA_CHECK(covered[i] || bytes[i] == 0);
    }
}

ASTIA_TEST(x86_64_signal_abi_keeps_register_order_and_frame_size) {
    ASTIA_CHECK(sizeof(astia_x86_64_signal_context_t) == 160);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rip) == 120);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rsp) == 128);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, rflags) == 136);
    ASTIA_CHECK(offsetof(astia_x86_64_signal_context_t, fault_rip) == 152);
    ASTIA_CHECK(sizeof(astia_sigaction_t) == 24);
    ASTIA_CHECK(sizeof(astia_siginfo_t) == 32);
    ASTIA_CHECK(ASTIA_SA_NOCLDWAIT == 2);
    ASTIA_CHECK(ASTIA_SA_SIGINFO == 4);
    ASTIA_CHECK(ASTIA_SA_NOCLDSTOP == 8);
    ASTIA_CHECK(ASTIA_SA_NODEFER == 0x40000000);
    ASTIA_CHECK(ASTIA_SA_RESETHAND == 0x80000000u);
    ASTIA_CHECK(ASTIA_CLD_STOPPED == 5);
    ASTIA_CHECK(ASTIA_CLD_CONTINUED == 6);
}
