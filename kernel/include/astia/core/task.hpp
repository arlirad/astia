#pragma once

#include <coroutine>

namespace astia::core {

    /// Minimal owning coroutine task. It is deliberately a building block:
    /// scheduling, cancellation and I/O awaiters belong above this type.
    class task {
        public:
        struct promise_type;
        using handle = std::coroutine_handle<promise_type>;

        task() = default;
        explicit task(handle coroutine) : coroutine_(coroutine) {}
        task(const task&)            = delete;
        task& operator=(const task&) = delete;

        task(task&& other) noexcept : coroutine_(other.coroutine_) {
            other.coroutine_ = {};
        }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                reset();
                coroutine_       = other.coroutine_;
                other.coroutine_ = {};
            }
            return *this;
        }

        ~task() { reset(); }

        bool valid() const { return coroutine_ && !coroutine_.done(); }
        bool done() const { return !coroutine_ || coroutine_.done(); }

        /// Resume one step. The caller owns scheduling and must not resume a
        /// task concurrently from multiple CPUs.
        bool resume() {
            if (done())
                return false;
            coroutine_.resume();
            return true;
        }

        void reset() {
            if (coroutine_)
                coroutine_.destroy();
            coroutine_ = {};
        }

        struct promise_type {
            task get_return_object() {
                return task(handle::from_promise(*this));
            }
            std::suspend_always initial_suspend() const noexcept { return {}; }
            std::suspend_always final_suspend() const noexcept { return {}; }
            void return_void() const noexcept {}
            void unhandled_exception() const noexcept { __builtin_trap(); }
        };

        private:
        handle coroutine_{};
    };

    struct yield_awaitable {
        bool await_ready() const noexcept { return false; }
        void await_suspend(task::handle) const noexcept {}
        void await_resume() const noexcept {}
    };

    inline yield_awaitable yield() noexcept { return {}; }

} // namespace astia::core
