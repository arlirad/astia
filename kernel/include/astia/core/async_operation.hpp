#pragma once

#include <astia/error.hpp>
#include <astia/optional.hpp>
#include <astia/core/resume_queue.hpp>

#include <coroutine>

namespace astia::core {

    /// One-shot awaitable completion. The producer may complete it from an
    /// IRQ-safe path; the consumer awaits it without a callback or lock.
    template <typename T>
    class async_operation {
        enum class state : u8 {
            pending,
            completed,
            canceled,
        };

        optional<T>              result_;
        u8                       state_ = static_cast<u8>(state::pending);
        std::coroutine_handle<>  continuation_{};

        bool finished() const {
            return __atomic_load_n(&state_, __ATOMIC_ACQUIRE) !=
                   static_cast<u8>(state::pending);
        }

        void wake() {
            auto continuation = __atomic_exchange_n(
                reinterpret_cast<void**>(&continuation_), nullptr, __ATOMIC_ACQ_REL);
            if (continuation != nullptr)
                std::coroutine_handle<>::from_address(continuation).resume();
        }

        void wake_deferred() {
            auto continuation = __atomic_exchange_n(
                reinterpret_cast<void**>(&continuation_), nullptr, __ATOMIC_ACQ_REL);
            if (continuation != nullptr)
                (void) schedule_resume(std::coroutine_handle<>::from_address(continuation));
        }

        public:
        async_operation() = default;
        async_operation(const async_operation&)            = delete;
        async_operation& operator=(const async_operation&) = delete;

        bool complete(const T& value) {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completed), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(value);
            wake();
            return true;
        }

        bool complete(T&& value) {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completed), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(static_cast<T&&>(value));
            wake();
            return true;
        }

        bool fail(error_t error) {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completed), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(error);
            wake();
            return true;
        }

        bool cancel() {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::canceled), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(error_t::eagain);
            wake();
            return true;
        }

        bool complete_deferred(const T& value) {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completed), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(value);
            wake_deferred();
            return true;
        }

        bool fail_deferred(error_t error) {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completed), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            result_ = optional<T>(error);
            wake_deferred();
            return true;
        }

        bool await_ready() const noexcept { return finished(); }

        bool await_suspend(std::coroutine_handle<> continuation) noexcept {
            __atomic_store_n(reinterpret_cast<void**>(&continuation_),
                             continuation.address(), __ATOMIC_RELEASE);
            if (finished()) {
                wake();
                return false;
            }
            return true;
        }

        optional<T> await_resume() {
            return static_cast<optional<T>&&>(result_);
        }

        bool done() const { return finished(); }
    };

} // namespace astia::core
