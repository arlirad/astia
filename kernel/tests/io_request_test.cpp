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

    astia::io::request pre_canceled(nullptr, 0, 0, 0, astia::io::direction::input);
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

    astia::io::request request(nullptr, 0, 0, 0, astia::io::direction::input);
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
