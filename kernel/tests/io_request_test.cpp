#include <astia/io.hpp>

#include <cassert>

namespace {
    int  wake_count   = 0;
    int  cancel_count = 0;
    void count_wake(void*) {
        ++wake_count;
    }
    void acknowledge_cancel(astia::io::request& request) {
        ++cancel_count;
        request.acknowledge();
        request.acknowledge();
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

    astia::io::request parked_request(nullptr, 0, 0, 0, astia::io::direction::input);
    auto*              flight = new astia::io::flight(parked_request);
    assert(parked_request.park_device());
    flight->release_owner();
    assert(flight->canceled());
    parked_request.acknowledge();

    astia::io::request synchronous_request(nullptr, 0, 0, 0, astia::io::direction::input);
    auto*              synchronous_flight = new astia::io::flight(synchronous_request);
    synchronous_request.cancel_handler_set(acknowledge_cancel);
    cancel_count = 0;
    assert(synchronous_request.park_device());
    synchronous_flight->release_owner();
    assert(cancel_count == 1);
    assert(synchronous_request.cancellation_requested());

    return 0;
}
