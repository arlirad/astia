#include <astia/core/task.hpp>
extern "C" void lifecycle_wait_for_release();

astia::core::task<void> module_main() {
    lifecycle_wait_for_release();

    co_return astia::error_t::enone;
}
