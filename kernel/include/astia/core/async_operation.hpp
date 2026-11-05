#pragma once

#include <astia/error.hpp>
#include <astia/optional.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/core/spinlock.hpp>

#include <coroutine>

namespace astia::core {

    /// One-shot awaitable completion. The producer may complete it from an
    /// IRQ-safe path; the consumer awaits it without a callback or lock.
    template <typename T>
    class async_operation {
        enum class state : u8 {
            pending,
            completing,
            completed,
            canceled,
        };

        optional<T>              result_;
        u8                       state_ = static_cast<u8>(state::pending);
        u8                       deferred_inflight_ = 0;
        std::coroutine_handle<>  continuation_{};

        bool finished() const {
            u8 value = __atomic_load_n(&state_, __ATOMIC_ACQUIRE);
            while (value == static_cast<u8>(state::completing))
                value = __atomic_load_n(&state_, __ATOMIC_ACQUIRE);
            return value != static_cast<u8>(state::pending);
        }

        void wake() {
            auto continuation = __atomic_exchange_n(
                reinterpret_cast<void**>(&continuation_), nullptr, __ATOMIC_ACQ_REL);
            if (continuation != nullptr)
                std::coroutine_handle<>::from_address(continuation).resume();
        }

        void wake_deferred() {
            __atomic_store_n(&deferred_inflight_, 1, __ATOMIC_RELEASE);
            auto continuation = __atomic_exchange_n(
                reinterpret_cast<void**>(&continuation_), nullptr, __ATOMIC_ACQ_REL);
            if (continuation != nullptr &&
                !schedule_resume(std::coroutine_handle<>::from_address(continuation)))
                __builtin_trap();
            __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
        }

        public:
        class awaiter {
            async_operation*       operation_;
            std::coroutine_handle<> continuation_{};

            public:
            explicit awaiter(async_operation* operation) : operation_(operation) {}
            awaiter(const awaiter&)            = delete;
            awaiter& operator=(const awaiter&) = delete;
            awaiter(awaiter&& other) noexcept
                : operation_(other.operation_), continuation_(other.continuation_) {
                other.operation_    = nullptr;
                other.continuation_ = {};
            }
            ~awaiter() {
                if (operation_ != nullptr)
                    operation_->detach(continuation_);
            }

            bool await_ready() const noexcept { return operation_->finished(); }

            bool await_suspend(std::coroutine_handle<> continuation) noexcept {
                continuation_ = continuation;
                return operation_->attach(continuation);
            }

            optional<T> await_resume() {
                return static_cast<optional<T>&&>(operation_->result_);
            }
        };

        async_operation() = default;
        async_operation(const async_operation&)            = delete;
        async_operation& operator=(const async_operation&) = delete;

        bool complete(const T& value) {
            if (!begin_completion())
                return false;
            result_ = optional<T>(value);
            publish(state::completed);
            wake();
            return true;
        }

        bool complete(T&& value) {
            if (!begin_completion())
                return false;
            result_ = optional<T>(static_cast<T&&>(value));
            publish(state::completed);
            wake();
            return true;
        }

        bool fail(error_t error) {
            if (!begin_completion())
                return false;
            result_ = optional<T>(error);
            publish(state::completed);
            wake();
            return true;
        }

        bool cancel() {
            if (!begin_completion())
                return false;
            result_ = optional<T>(error_t::eagain);
            publish(state::canceled);
            wake();
            return true;
        }

        bool complete_deferred(const T& value) {
            if (!begin_completion())
                return false;
            result_ = optional<T>(value);
            publish(state::completed);
            wake_deferred();
            return true;
        }

        bool fail_deferred(error_t error) {
            if (!begin_completion())
                return false;
            result_ = optional<T>(error);
            publish(state::completed);
            wake_deferred();
            return true;
        }

        bool await_ready() const noexcept { return finished(); }

        awaiter operator co_await() noexcept {
            return awaiter(this);
        }

        optional<T> await_resume() {
            return static_cast<optional<T>&&>(result_);
        }

        bool done() const { return finished(); }

        private:
        bool begin_completion() {
            u8 expected = static_cast<u8>(state::pending);
            if (!__atomic_compare_exchange_n(&state_, &expected,
                                              static_cast<u8>(state::completing), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            return true;
        }

        void publish(state value) {
            __atomic_store_n(&state_, static_cast<u8>(value), __ATOMIC_RELEASE);
        }

        bool attach(std::coroutine_handle<> continuation) noexcept {
            if (finished())
                return false;
            void* expected = nullptr;
            if (!__atomic_compare_exchange_n(reinterpret_cast<void**>(&continuation_),
                                              &expected, continuation.address(), false,
                                              __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
                return false;
            if (finished()) {
                void* value = continuation.address();
                if (__atomic_compare_exchange_n(
                        reinterpret_cast<void**>(&continuation_), &value, nullptr, false,
                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    return false;
                return true;
            }
            return true;
        }

        void detach(std::coroutine_handle<> continuation) noexcept {
            if (!continuation)
                return;
            void* expected = continuation.address();
            const bool detached = __atomic_compare_exchange_n(
                reinterpret_cast<void**>(&continuation_), &expected, nullptr, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            if (!detached) {
                while (__atomic_load_n(&deferred_inflight_, __ATOMIC_ACQUIRE) != 0)
                    ASTIA_CPU_RELAX();
            }
            cancel_resume(continuation);
        }
    };

} // namespace astia::core
