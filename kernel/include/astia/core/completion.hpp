#pragma once

#include <astia/core/coroutine_lifecycle.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/error.hpp>
#include <astia/optional.hpp>

#include <coroutine>

namespace astia::core {

    using completion_detach_observer = void (*)(void* context);

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

        optional<T>                result_;
        u8                         state_             = static_cast<u8>(state::pending);
        u8                         deferred_inflight_ = 0;
        u8                         detach_inflight_   = 0;
        u64                        generation_        = 1;
        volatile u32               awaiter_refs_      = 0;
        std::coroutine_handle<>    continuation_{};
        completion_detach_observer detach_observer_ = nullptr;
        void*                      detach_context_  = nullptr;

        bool finished() const {
            u8 value = __atomic_load_n(&state_, __ATOMIC_ACQUIRE);
            while (value == static_cast<u8>(state::completing))
                value = __atomic_load_n(&state_, __ATOMIC_ACQUIRE);
            return value != static_cast<u8>(state::pending);
        }

        void wake() {
            for (;;) {
                auto* value = __atomic_load_n(reinterpret_cast<void**>(&continuation_),
                                              __ATOMIC_ACQUIRE);
                if (value == nullptr)
                    return;
                const auto handle = std::coroutine_handle<>::from_address(value);
                if (!resume_begin(handle))
                    return;
                void* expected = value;
                if (!__atomic_compare_exchange_n(reinterpret_cast<void**>(&continuation_),
                                                 &expected, nullptr, false,
                                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    resume_end(handle);
                    continue;
                }
                handle.resume();
                resume_end(handle);
                return;
            }
        }

        void wake_deferred() {
            const u64 generation = __atomic_load_n(&generation_, __ATOMIC_ACQUIRE);
            const u64 key        = reinterpret_cast<u64>(this);
            __atomic_store_n(&deferred_inflight_, 1, __ATOMIC_RELEASE);
            for (;;) {
                auto* value = __atomic_load_n(reinterpret_cast<void**>(&continuation_),
                                              __ATOMIC_ACQUIRE);
                if (value == nullptr) {
                    __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
                    return;
                }
                const auto handle = std::coroutine_handle<>::from_address(value);
                if (!resume_begin(handle)) {
                    __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
                    return;
                }
                void* expected = value;
                if (!__atomic_compare_exchange_n(reinterpret_cast<void**>(&continuation_),
                                                 &expected, nullptr, false,
                                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    resume_end(handle);
                    continue;
                }
                const resume_schedule result =
                    schedule_resume_owned(handle, generation, key);
                if (result == resume_schedule::queued) {
                    __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
                    return;
                }
                if (result == resume_schedule::duplicate) {
                    resume_end(handle);
                    __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
                    return;
                }
                __atomic_store_n(&deferred_inflight_, 0, __ATOMIC_RELEASE);
                handle.resume();
                resume_end(handle);
                return;
            }
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
                if (completion_ != nullptr && generation_ != 0)
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

        u32 awaiter_count() const {
            return __atomic_load_n(&awaiter_refs_, __ATOMIC_ACQUIRE);
        }

        bool detach_inflight() const {
            return __atomic_load_n(&detach_inflight_, __ATOMIC_ACQUIRE) != 0;
        }

        void clear_detach_inflight() noexcept {
            __atomic_store_n(&detach_inflight_, 0, __ATOMIC_RELEASE);
        }

        void set_awaiter_observer(completion_detach_observer observer,
                                  void*                      context) noexcept {
            __atomic_store_n(&detach_context_, context, __ATOMIC_RELEASE);
            __atomic_store_n(&detach_observer_, observer, __ATOMIC_RELEASE);
        }

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

        u64 attach(std::coroutine_handle<> continuation) noexcept {
            if (finished())
                return 0;
            const u64 generation = __atomic_load_n(&generation_, __ATOMIC_ACQUIRE);
            __atomic_add_fetch(&awaiter_refs_, 1, __ATOMIC_ACQ_REL);
            void* expected = nullptr;
            if (!__atomic_compare_exchange_n(reinterpret_cast<void**>(&continuation_),
                                             &expected, continuation.address(), false,
                                             __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
                __atomic_sub_fetch(&awaiter_refs_, 1, __ATOMIC_ACQ_REL);
                return 0;
            }
            if (finished()) {
                void* value = continuation.address();
                if (__atomic_compare_exchange_n(reinterpret_cast<void**>(&continuation_),
                                                &value, nullptr, false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    __atomic_sub_fetch(&awaiter_refs_, 1, __ATOMIC_ACQ_REL);
                    return 0;
                }
                return generation;
            }
            return generation;
        }

        void detach(std::coroutine_handle<> continuation, u64 generation) noexcept {
            if (!continuation)
                return;
            __atomic_add_fetch(&generation_, 1, __ATOMIC_ACQ_REL);
            void*      expected = continuation.address();
            const bool detached = __atomic_compare_exchange_n(
                reinterpret_cast<void**>(&continuation_), &expected, nullptr, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            if (!detached) {
                while (__atomic_load_n(&deferred_inflight_, __ATOMIC_ACQUIRE) != 0)
                    ASTIA_CPU_RELAX();
            }
            cancel_resume(continuation, generation, reinterpret_cast<u64>(this));
            __atomic_add_fetch(&detach_inflight_, 1, __ATOMIC_ACQ_REL);
            if (__atomic_sub_fetch(&awaiter_refs_, 1, __ATOMIC_ACQ_REL) == 0) {
                const auto observer =
                    __atomic_load_n(&detach_observer_, __ATOMIC_ACQUIRE);
                void* context = __atomic_load_n(&detach_context_, __ATOMIC_ACQUIRE);
                if (observer != nullptr) {
                    observer(context);
                    return;
                }
            }
            __atomic_store_n(&detach_inflight_, 0, __ATOMIC_RELEASE);
        }
    };

} // namespace astia::core
