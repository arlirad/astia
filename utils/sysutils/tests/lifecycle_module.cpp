#include <astia/core/task.hpp>

static int lifecycle_counter = 0;

astia::core::task<void> module_main() {
    ++lifecycle_counter;

    co_return lifecycle_counter == 1 ? astia::error_t::enone : astia::error_t::eio;
}
