#include <astia/io.hpp>

#include <cassert>

namespace {
    int  wake_count = 0;
    void count_wake(void*) {
        ++wake_count;
    }
}

int main() {
    astia::core::clsource source;
    const auto            initial_token = source.token();
    assert(initial_token.valid());
    assert(!initial_token.cancellation_requested());
    assert(source.cancel(astia::core::cancel_reason::detach));
    assert(initial_token.cancellation_requested());
    assert(initial_token.requested());
    assert(initial_token.reason() == astia::core::cancel_reason::detach);
    assert(!source.cancel(astia::core::cancel_reason::timeout));
    const astia::u64 initial_generation = initial_token.generation();
    source.reset();
    const auto fresh_token = source.token();
    assert(fresh_token.valid());
    assert(fresh_token.generation() != initial_generation);
    assert(initial_token.stale());
    assert(!initial_token.cancellation_requested());
    assert(initial_token.reason() == astia::core::cancel_reason::none);
    assert(!fresh_token.cancellation_requested());

    // Child sources observe their own cancellation and every ancestor's,
    // while siblings stay independent.
    astia::core::clsource root;
    astia::core::clsource first_op{root};
    astia::core::clsource second_op{root};
    const auto            root_token   = root.token();
    const auto            first_token  = first_op.token();
    const auto            second_token = second_op.token();

    assert(first_op.parent() == &root);
    assert(first_token.valid() && second_token.valid());
    assert(!first_token.cancellation_requested());

    assert(first_op.cancel(astia::core::cancel_reason::timeout));
    assert(first_token.cancellation_requested());
    assert(first_token.reason() == astia::core::cancel_reason::timeout);
    assert(!second_token.cancellation_requested());
    assert(!root_token.cancellation_requested());

    assert(root.cancel(astia::core::cancel_reason::shutdown));
    assert(root_token.cancellation_requested());
    assert(root_token.reason() == astia::core::cancel_reason::shutdown);
    assert(second_token.cancellation_requested());
    assert(second_token.reason() == astia::core::cancel_reason::shutdown);
    // The nearest cancelled source supplies the reason.
    assert(first_token.reason() == astia::core::cancel_reason::timeout);

    // A child created after the parent was cancelled sees it immediately.
    astia::core::clsource late{root};
    const auto            late_token = late.token();
    assert(late_token.cancellation_requested());
    assert(late_token.reason() == astia::core::cancel_reason::shutdown);

    // Resetting an ancestor stales every descendant token.
    root.reset();
    assert(root_token.stale());
    assert(first_token.stale());
    assert(second_token.stale());
    assert(late_token.stale());
    assert(!first_token.cancellation_requested());

    // Resetting a child only stales that child.
    astia::core::clsource local;
    astia::core::clsource child{local};
    const auto            local_token = local.token();
    const auto            child_token = child.token();
    child.reset();
    assert(child_token.stale());
    assert(local_token.valid());
    assert(child.token().valid());

    astia::core::clsource pre_canceled_source;

    astia::io::request pre_canceled(pre_canceled_source, nullptr, 0, 0, 0,
                                    astia::io::direction::input);
    const auto         request_token = pre_canceled.cancellation_token();
    assert(request_token.valid());
    assert(pre_canceled.cancel_device(false, astia::core::cancel_reason::owner));
    assert(pre_canceled.cancellation_requested());
    assert(pre_canceled.cancellation_reason() == astia::core::cancel_reason::owner);
    pre_canceled.cancel_acknowledge();
    assert(!pre_canceled.cancel_device());

    astia::io::generation generations;
    const astia::u64      first = generations.issue();
    assert(first == 1);
    assert(generations.current(first));
    const astia::u64 second = generations.issue();
    assert(generations.current(second));
    assert(!generations.current(first));
    assert(!generations.current(0));

    astia::core::clsource request_source;

    astia::io::request request(request_source, nullptr, 0, 0, 0,
                               astia::io::direction::input);
    request.poll_interval_ns(1000);
    assert(request.poll_interval_set());
    assert(request.poll_interval_ns() == 1000);
    assert(!request.poll_wakeup());
    request.poll_wakeup(true);
    assert(request.poll_wakeup());
    request.poll_wakeup(false);
    assert(!request.poll_wakeup());
    request.generation(17);
    request.wake_handler_set(count_wake, nullptr);
    wake_count = 0;
    assert(!request.wake(16));
    assert(wake_count == 0);
    assert(request.wake(17));
    assert(wake_count == 1);

    return 0;
}
