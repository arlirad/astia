#include <astia/core/task.hpp>
extern "C" void lifecycle_reentrant_outer_handler();
extern "C" int  atexit(void (*)(void));

astia::core::task<void> module_main() {
    co_return atexit(lifecycle_reentrant_outer_handler) == 0 ? astia::error_t::enone
                                                             : astia::error_t::eio;
}
