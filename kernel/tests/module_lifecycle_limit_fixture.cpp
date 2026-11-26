#include <astia/core/task.hpp>
extern "C" int  atexit(void (*)(void));
extern "C" void lifecycle_record(int);

static void noop() {}

astia::core::task<void> module_main() {
    int failures = 0;

    for (int i = 0; i < 64; ++i) {
        failures += atexit(noop) != 0;
    }

    failures += atexit(noop) == 0;

    lifecycle_record(400 + failures);

    co_return failures == 0 ? astia::error_t::enone : astia::error_t::eio;
}
