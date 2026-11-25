// Pins the wait_park contract against the lost-wakeup race: a thread may only
// park when no continuation is already queued in _pending_resume.  The test
// drives the shipping thread.cpp implementation directly, so the scheduler,
// timers, and QEMU are not involved.

#include <astia/arch/context.hpp>
#include <astia/mm/heap.hpp>

#include "support/test.hpp"
#include "thread.hpp"

#include <coroutine>
#include <cstdlib>

namespace astia::arch {
    struct context {};

    context* context_create() {
        return nullptr;
    }

    void context_destroy(context*) {}
} // namespace astia::arch

namespace astia::mm::heap {
    void free(void*) {}
} // namespace astia::mm::heap

namespace {
    struct probe {
        struct promise_type {
            probe get_return_object() {
                return probe{std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_always initial_suspend() noexcept {
                return {};
            }
            std::suspend_always final_suspend() noexcept {
                return {};
            }
            void return_void() {}
            void unhandled_exception() {
                std::abort();
            }
        };

        std::coroutine_handle<promise_type> handle;

        ~probe() {
            if (handle)
                handle.destroy();
        }
    };

    probe resume_probe(bool& resumed) {
        resumed = true;
        co_return;
    }
} // namespace

ASTIA_TEST(thread_park_without_pending_resume_blocks) {
    astia::proc::thread value(nullptr, 1);

    ASTIA_CHECK(value.park_unless_woken());
    ASTIA_CHECK(value.state() == astia::proc::thread_state::blocked);
}

ASTIA_TEST(thread_park_with_pending_resume_keeps_it_for_the_caller) {
    astia::proc::thread value(nullptr, 2);
    bool                resumed = false;
    probe               pending = resume_probe(resumed);

    value.request_resume(pending.handle);
    ASTIA_CHECK(!value.park_unless_woken());
    ASTIA_CHECK(value.state() != astia::proc::thread_state::blocked);
    ASTIA_CHECK(!resumed);

    value.resume_pending();
    ASTIA_CHECK(resumed);
}

ASTIA_TEST(thread_park_after_draining_a_resume_blocks) {
    astia::proc::thread value(nullptr, 3);
    bool                resumed = false;
    probe               pending = resume_probe(resumed);

    value.request_resume(pending.handle);
    value.resume_pending();
    ASTIA_CHECK(resumed);

    ASTIA_CHECK(value.park_unless_woken());
    ASTIA_CHECK(value.state() == astia::proc::thread_state::blocked);
}
