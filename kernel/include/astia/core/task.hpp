#pragma once

#include <coroutine>
#include <new>
#include <astia/common.hpp>
#include <astia/error.hpp>
#include <astia/optional.hpp>

namespace astia::core {

    template <typename T = void>
    class task;

    template <typename T>
    class task {
        public:
        struct promise_type;
        using handle = std::coroutine_handle<promise_type>;

        struct promise_type {
            T               value{};
            error_t         result = error_t::enone;
            std::coroutine_handle<> continuation{};
            handle            waiting{};

            static void* operator new(usize size) noexcept {
                return ::operator new(size);
            }
            static void operator delete(void* value) noexcept { ::operator delete(value); }
            task get_return_object() { return task(handle::from_promise(*this)); }
            std::suspend_always initial_suspend() const noexcept { return {}; }
            struct final_awaiter {
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(handle current) const noexcept {
                    return current.promise().continuation ? current.promise().continuation
                                                           : std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            final_awaiter final_suspend() const noexcept { return {}; }
            void return_value(T value_) noexcept { value = static_cast<T&&>(value_); }
            void return_value(error_t error) noexcept { result = error; }
            void unhandled_exception() const noexcept { __builtin_trap(); }
        };

        task() = default;
        explicit task(handle value) : coroutine_(value) {}
        explicit task(error_t error) : error_(error) {}
        task(const task&)            = delete;
        task& operator=(const task&) = delete;
        task(task&& other) noexcept : coroutine_(other.coroutine_), error_(other.error_) {
            other.coroutine_ = {};
            other.error_     = error_t::enone;
        }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                reset();
                coroutine_       = other.coroutine_;
                error_            = other.error_;
                other.coroutine_ = {};
                other.error_      = error_t::enone;
            }
            return *this;
        }
        ~task() { reset(); }

        bool done() const { return !coroutine_ || coroutine_.done(); }
        bool valid() const { return coroutine_ && !coroutine_.done(); }
        error_t error() const {
            if (error_ != error_t::enone)
                return error_;
            return coroutine_ ? coroutine_.promise().result : error_t::enone;
        }
        bool failed() const { return error() != error_t::enone; }
        T* value() { return coroutine_ ? &coroutine_.promise().value : nullptr; }

        bool resume() {
            if (done())
                return false;
            if (coroutine_.promise().waiting && !coroutine_.promise().waiting.done())
                coroutine_.promise().waiting.resume();
            else
                coroutine_.resume();
            return true;
        }

        struct awaiter {
            handle coroutine;
            handle parent;
            bool await_ready() const noexcept { return !coroutine || coroutine.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
                parent = handle::from_address(caller.address());
                parent.promise().waiting = coroutine;
                coroutine.promise().continuation = caller;
                return coroutine;
            }
            optional<T> await_resume() noexcept {
                if (!coroutine)
                    return error_t::enomem;
                const error_t error = coroutine.promise().result;
                optional<T> result = error == error_t::enone
                                         ? optional<T>(coroutine.promise().value)
                                         : optional<T>(error);
                coroutine.destroy();
                if (parent)
                    parent.promise().waiting = {};
                coroutine = {};
                return result;
            }
        };
        awaiter operator co_await() && noexcept {
            awaiter value{coroutine_, {}};
            coroutine_ = {};
            return value;
        }
        void reset() {
            if (coroutine_)
                coroutine_.destroy();
            coroutine_ = {};
        }

        private:
        handle  coroutine_{};
        error_t error_ = error_t::enone;
    };

    /// Minimal owning coroutine task. It is deliberately a building block:
    /// scheduling, cancellation and I/O awaiters belong above this type.
    template <>
    class task<void> {
        public:
        static constexpr usize frame_slot_bytes = 2048;
        static constexpr usize frame_slot_count = 32;
        struct promise_type;
        using handle = std::coroutine_handle<promise_type>;

        task() = default;
        explicit task(handle coroutine) : coroutine_(coroutine) {}
        explicit task(error_t error) : error_(error) {}
        task(const task&)            = delete;
        task& operator=(const task&) = delete;

        task(task&& other) noexcept : coroutine_(other.coroutine_), error_(other.error_) {
            other.coroutine_ = {};
            other.error_     = error_t::enone;
        }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                reset();
                coroutine_       = other.coroutine_;
                error_           = other.error_;
                other.coroutine_ = {};
                other.error_     = error_t::enone;
            }
            return *this;
        }

        ~task() { reset(); }

        bool valid() const { return coroutine_ && !coroutine_.done(); }
        bool done() const { return !coroutine_ || coroutine_.done(); }
        bool failed() const { return error() != error_t::enone; }
        error_t error() const {
            if (error_ != error_t::enone)
                return error_;
            return coroutine_ ? coroutine_.promise().result : error_t::enone;
        }

        /// Resume one step. The caller owns scheduling and must not resume a
        /// task concurrently from multiple CPUs.
        bool resume() {
            if (done())
                return false;
            auto waiting = coroutine_.promise().waiting;
            if (waiting && !waiting.done())
                waiting.resume();
            else
                coroutine_.resume();
            return true;
        }

        struct awaiter {
            handle coroutine;
            handle parent;

            bool await_ready() const noexcept {
                return !coroutine || coroutine.done();
            }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
                parent = handle::from_address(caller.address());
                parent.promise().waiting = coroutine;
                coroutine.promise().continuation = caller;
                return coroutine;
            }
            error_t await_resume() noexcept {
                if (!coroutine)
                    return error_t::enomem;
                const error_t result = coroutine.promise().result;
                coroutine.destroy();
                coroutine = {};
                if (parent)
                    parent.promise().waiting = {};
                return result;
            }
        };

        awaiter operator co_await() && noexcept {
            awaiter result{coroutine_};
            coroutine_ = {};
            return result;
        }

        void reset() {
            if (coroutine_)
                coroutine_.destroy();
            coroutine_ = {};
        }

        struct promise_type {
            static void* operator new(usize size) noexcept {
                return allocate_frame(size);
            }
            static void* operator new(usize size, const std::nothrow_t&) noexcept {
                return allocate_frame(size);
            }
            static void operator delete(void* value) noexcept {
                release_frame(value);
            }
            static task get_return_object_on_allocation_failure() noexcept {
                return task(error_t::enomem);
            }
            task get_return_object() {
                return task(handle::from_promise(*this));
            }
            std::suspend_always initial_suspend() const noexcept { return {}; }
            struct final_awaiter {
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(handle current) const noexcept {
                    return current.promise().continuation
                               ? current.promise().continuation
                               : std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            final_awaiter final_suspend() const noexcept { return {}; }
            void return_value(error_t error) noexcept { result = error; }
            void unhandled_exception() const noexcept { __builtin_trap(); }
            error_t result = error_t::enone;
            std::coroutine_handle<> continuation{};
            handle waiting{};
        };

        private:
        struct alignas(16) frame_slot {
            u32 next;
            u8  storage[frame_slot_bytes - sizeof(u32)];
        };

        inline static frame_slot frame_pool_[frame_slot_count]{};
        inline static u32        frame_head_ = 0;
        inline static u32        frame_initialized_ = 0;
        inline static u32        pool_allocations_ = 0;

        static void initialize_pool() {
            u32 state = __atomic_load_n(&frame_initialized_, __ATOMIC_ACQUIRE);
            if (state == 2)
                return;
            u32 expected = 0;
            if (__atomic_compare_exchange_n(&frame_initialized_, &expected, 1, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                for (u32 i = 0; i + 1 < frame_slot_count; ++i)
                    frame_pool_[i].next = i + 1;
                frame_pool_[frame_slot_count - 1].next = frame_slot_count;
                __atomic_store_n(&frame_head_, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&frame_initialized_, 2, __ATOMIC_RELEASE);
            }
            else {
                while (__atomic_load_n(&frame_initialized_, __ATOMIC_ACQUIRE) != 2) {
                }
            }
        }

        static void* allocate_frame(usize size) noexcept {
            if (size > frame_slot_bytes)
                return nullptr;
            initialize_pool();
            u32 head = __atomic_load_n(&frame_head_, __ATOMIC_ACQUIRE);
            while (head != frame_slot_count) {
                u32 next = frame_pool_[head].next;
                if (__atomic_compare_exchange_n(&frame_head_, &head, next, false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    __atomic_add_fetch(&pool_allocations_, 1, __ATOMIC_RELAXED);
                    return frame_pool_[head].storage;
                }
            }
            return nullptr;
        }

        static bool owns_frame(void* value, u32& index) {
            const usize address = reinterpret_cast<usize>(value);
            const usize first   = reinterpret_cast<usize>(frame_pool_[0].storage);
            const usize last = reinterpret_cast<usize>(frame_pool_[frame_slot_count - 1].storage) +
                               sizeof(frame_pool_[frame_slot_count - 1].storage);
            if (address < first || address >= last)
                return false;
            const usize stride = sizeof(frame_slot);
            const usize offset = address - first;
            if (offset % stride != 0)
                return false;
            index = static_cast<u32>(offset / stride);
            return index < frame_slot_count;
        }

        static void release_frame(void* value) noexcept {
            u32 index = 0;
            if (!owns_frame(value, index)) {
                return;
            }
            u32 head = __atomic_load_n(&frame_head_, __ATOMIC_RELAXED);
            do {
                frame_pool_[index].next = head;
            } while (!__atomic_compare_exchange_n(&frame_head_, &head, index, false,
                                                  __ATOMIC_RELEASE, __ATOMIC_RELAXED));
        }

        static void* operator new(usize size) noexcept { return allocate_frame(size); }
        static void operator delete(void* value) noexcept { release_frame(value); }

        public:
        static u32 pooled_frame_allocations() {
            return __atomic_load_n(&pool_allocations_, __ATOMIC_RELAXED);
        }

        private:
        handle coroutine_{};
        error_t error_ = error_t::enone;
    };

    struct yield_awaitable {
        bool await_ready() const noexcept { return false; }
        void await_suspend(task<>::handle) const noexcept {}
        void await_resume() const noexcept {}
    };

    inline yield_awaitable yield() noexcept { return {}; }

} // namespace astia::core
