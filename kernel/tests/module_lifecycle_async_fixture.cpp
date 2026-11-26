#include <astia/core/task.hpp>

#include <coroutine>

extern "C" void lifecycle_record(int);
extern "C" int  atexit(void (*)(void));
extern "C" void lifecycle_async_gate(void* handle);

namespace {
    struct host_gate {
        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            lifecycle_async_gate(handle.address());
        }

        void await_resume() const noexcept {}
    };

    astia::core::task<void> inner() {
        lifecycle_record(501);
        co_await host_gate{};
        lifecycle_record(502);

        co_return astia::error_t::enone;
    }

    void lifecycle_async_exit_handler() {
        lifecycle_record(503);
    }
}

astia::core::task<void> module_main() {
    lifecycle_record(500);

    const astia::error_t value = co_await inner();

    if (value != astia::error_t::enone)
        co_return value;

    lifecycle_record(504);

    if (atexit(lifecycle_async_exit_handler) != 0)
        co_return astia::error_t::eio;

    co_return astia::error_t::enone;
}
