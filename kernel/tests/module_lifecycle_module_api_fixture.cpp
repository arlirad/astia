#include <astia/core/task.hpp>

extern "C" int  lifecycle_module_api_probe();
extern "C" void lifecycle_irq_entry();

astia::core::task<void> module_main() {
    (void) lifecycle_module_api_probe();

    co_return astia::error_t::enone;
}
