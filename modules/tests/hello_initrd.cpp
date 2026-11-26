#include <astia/core/task.hpp>
static int counter = 40;

astia::core::task<void> module_main() {
    counter += 2;

    co_return counter == 42 ? astia::error_t::enone : astia::error_t::eio;
}
