#include <astia/core/task.hpp>
using lifecycle_callback = void (*)();

static void lifecycle_priority_callback() {}

__attribute__((section(".fini_array."),
               used)) lifecycle_callback lifecycle_priority_entry =
    lifecycle_priority_callback;

astia::core::task<void> module_main() {
    co_return astia::error_t::enone;
}
