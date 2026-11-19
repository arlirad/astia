#include <astia/io.hpp>

#include <cassert>

namespace {
    int  wake_count = 0;
    void count_wake(void*) {
        ++wake_count;
    }
}

int main() {
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
