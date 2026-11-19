#include <astia/core/spinlock.hpp>

#include "test.hpp"

#include <atomic>
#include <thread>

namespace {
    class bridge_gate {
        public:
        bool acquire() {
            astia::core::spinlock_guard guard(lock_);
            if (!used_ || closing_)
                return false;
            ++callbacks_;
            return true;
        }

        void release() {
            astia::core::spinlock_guard guard(lock_);
            --callbacks_;
        }

        bool close() {
            astia::core::spinlock_guard guard(lock_);
            if (!used_ || closing_)
                return false;
            closing_ = true;
            return true;
        }

        void wait_callbacks() {
            for (;;) {
                {
                    astia::core::spinlock_guard guard(lock_);
                    if (callbacks_ == 0)
                        return;
                }
                astia::core::spinlock_guard guard(lock_);
            }
        }

        void recycle() {
            astia::core::spinlock_guard guard(lock_);
            used_    = true;
            closing_ = false;
        }

        private:
        astia::core::spinlock lock_;
        unsigned              callbacks_ = 0;
        bool                  used_      = true;
        bool                  closing_   = false;
    };
}

ASTIA_TEST(module_bridge_close_waits_for_late_completion) {
    bridge_gate       bridge;
    std::atomic<bool> callback_entered{false};
    std::atomic<bool> allow_callback{false};
    std::atomic<bool> close_done{false};

    std::thread callback([&] {
        ASTIA_CHECK(bridge.acquire());
        callback_entered.store(true, std::memory_order_release);
        while (!allow_callback.load(std::memory_order_acquire))
            std::this_thread::yield();
        bridge.release();
    });
    while (!callback_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::thread closer([&] {
        ASTIA_CHECK(bridge.close());
        bridge.wait_callbacks();
        bridge.recycle();
        close_done.store(true, std::memory_order_release);
    });
    while (!close_done.load(std::memory_order_acquire)) {
        if (!bridge.acquire())
            break;
        bridge.release();
    }
    ASTIA_CHECK(!close_done.load(std::memory_order_acquire));
    allow_callback.store(true, std::memory_order_release);
    callback.join();
    closer.join();

    ASTIA_CHECK(close_done.load(std::memory_order_acquire));
    ASTIA_CHECK(bridge.acquire());
    bridge.release();
}
