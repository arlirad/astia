#include <astia/core/task.hpp>
int kernel_add(int a, int b);

static int counter = 40;

astia::core::task<void> module_main() {
    counter += 2;

    (void) kernel_add(counter, 0);

    co_return astia::error_t::enone;
}
