#pragma once

#include <astia/atomic.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/error.hpp>
#include <astia/optional.hpp>

#include <coroutine>

namespace astia::core {
    /// One-shot awaitable completion owning its optional result and awaiting
    /// continuation handle, but not the coroutine frame that owns the handle.
    /// The producer may complete it from an IRQ-safe path; the consumer awaits
    /// it without a callback or lock.
    template <typename T>
    class completion {
        enum class state : u8 {
            pending,
            completing,
            completed,
            canceled,
        };

        optional<T> result_;
        mutable u8  state_             = static_cast<u8>(state::pending);
        u8          deferred_inflight_ = 0;
        u64         generation_        = 1;
        void*       continuation_      = nullptr;

        bool finished() const {
            u8 value = astia::atomic_ref<u8>(state_).load(astia::memory_order::acquire);

            while (value == static_cast<u8>(state::completing)) {
                value = astia::atomic_ref<u8>(state_).load(astia::memory_order::acquire);
            }

            return value != static_cast<u8>(state::pending);
        }

        void wake() {
            auto continuation = astia::atomic_ref<void*>(continuation_)
                                    .exchange(nullptr, astia::memory_order::acq_rel);
            if (continuation != nullptr)
                std::coroutine_handle<>::from_address(continuation).resume();
        }

        void wake_deferred() {
            astia::atomic_ref<u8>(deferred_inflight_)
                .store(1, astia::memory_order::release);

            const u64 generation =
                astia::atomic_ref<u64>(generation_).load(astia::memory_order::acquire);

            auto continuation = astia::atomic_ref<void*>(continuation_)
                                    .exchange(nullptr, astia::memory_order::acq_rel);
            if (continuation != nullptr) {
                const auto handle = std::coroutine_handle<>::from_address(continuation);

                if (!schedule_resume(handle, generation)) {
                    // The fallback resumes inline: the awaiter (and this
                    // completion) may be destroyed by the call, so release the
                    // in-flight flag first and never touch this object again.
                    astia::atomic_ref<u8>(deferred_inflight_)
                        .store(0, astia::memory_order::release);

                    handle.resume();

                    return;
                }
            }

            astia::atomic_ref<u8>(deferred_inflight_)
                .store(0, astia::memory_order::release);
        }

        public:
        class awaiter {
            completion*             completion_;
            std::coroutine_handle<> continuation_{};
            u64                     generation_ = 0;

            public:
            explicit awaiter(completion* completion) : completion_(completion) {}

            awaiter(const awaiter&)            = delete;
            awaiter& operator=(const awaiter&) = delete;

            awaiter(awaiter&& other) noexcept
                : completion_(other.completion_), continuation_(other.continuation_),
                  generation_(other.generation_) {
                other.completion_   = nullptr;
                other.continuation_ = {};
                other.generation_   = 0;
            }

            ~awaiter() {
                if (completion_ != nullptr)
                    completion_->detach(continuation_, generation_);
            }

            bool await_ready() const noexcept {
                return completion_->finished();
            }

            bool await_suspend(std::coroutine_handle<> continuation) noexcept {
                continuation_ = continuation;
                generation_   = completion_->attach(continuation);

                return generation_ != 0;
            }

            optional<T> await_resume() {
                return static_cast<optional<T>&&>(completion_->result_);
            }
        };

        completion()                             = default;
        completion(const completion&)            = delete;
        completion& operator=(const completion&) = delete;

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

        bool await_ready() const noexcept {
            return finished();
        }

        awaiter operator co_await() noexcept {
            return awaiter(this);
        }

        optional<T> await_resume() {
            return static_cast<optional<T>&&>(result_);
        }

        bool done() const {
            return finished();
        }

        private:
        bool begin_completion() {
            u8 expected = static_cast<u8>(state::pending);

            if (!astia::atomic_ref<u8>(state_).compare_exchange_strong(
                    expected, static_cast<u8>(state::completing),
                    astia::memory_order::acq_rel, astia::memory_order::acquire))
                return false;

            return true;
        }

        void publish(state value) {
            astia::atomic_ref<u8>(state_).store(static_cast<u8>(value),
                                                astia::memory_order::release);
        }

        u64 attach(std::coroutine_handle<> continuation) noexcept {
            if (finished())
                return 0;

            const u64 generation =
                astia::atomic_ref<u64>(generation_).load(astia::memory_order::acquire);

            void* expected = nullptr;

            if (!astia::atomic_ref<void*>(continuation_)
                     .compare_exchange_strong(expected, continuation.address(),
                                              astia::memory_order::release,
                                              astia::memory_order::acquire))
                return 0;

            if (finished()) {
                void* value = continuation.address();

                if (astia::atomic_ref<void*>(continuation_)
                        .compare_exchange_strong(value, nullptr,
                                                 astia::memory_order::acq_rel,
                                                 astia::memory_order::acquire))
                    return 0;

                return generation;
            }

            return generation;
        }

        void detach(std::coroutine_handle<> continuation, u64 generation) noexcept {
            if (!continuation)
                return;

            astia::atomic_ref<u64>(generation_)
                .add_fetch(1, astia::memory_order::acq_rel);

            void* expected = continuation.address();

            const bool detached = astia::atomic_ref<void*>(continuation_)
                                      .compare_exchange_strong(
                                          expected, nullptr, astia::memory_order::acq_rel,
                                          astia::memory_order::acquire);
            if (!detached) {
                while (astia::atomic_ref<u8>(deferred_inflight_)
                           .load(astia::memory_order::acquire) != 0) {
                    arch::cpu::relax();
                }
            }

            cancel_resume(continuation, generation);
        }
    };
} // namespace astia::core
