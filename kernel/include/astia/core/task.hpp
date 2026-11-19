#pragma once

#include <astia/common.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/error.hpp>
#include <astia/optional.hpp>
#include <astia/resource_limits.hpp>

#include <coroutine>
#include <new>

namespace astia::core {

    namespace detail {
        struct resume_context;
    }

    struct task_link;

    namespace detail {
        struct resume_context {
            bool                    active = false;
            std::coroutine_handle<> pending{};
            task_link*              links = nullptr;
        };
    }

    struct task_link {
        std::coroutine_handle<> continuation{};
        std::coroutine_handle<> waiting{};
        task_link*              parent          = nullptr;
        detail::resume_context* resume          = nullptr;
        task_link*              resume_previous = nullptr;
        task_link*              resume_next     = nullptr;
    };

    namespace detail {
        inline void detach_resume_context(task_link& link) noexcept {
            auto* context = link.resume;
            if (context == nullptr)
                return;
            if (link.resume_previous != nullptr)
                link.resume_previous->resume_next = link.resume_next;
            else
                context->links = link.resume_next;
            if (link.resume_next != nullptr)
                link.resume_next->resume_previous = link.resume_previous;
            link.resume          = nullptr;
            link.resume_previous = nullptr;
            link.resume_next     = nullptr;
        }

        inline void attach_resume_context(task_link&      link,
                                          resume_context& context) noexcept {
            if (link.resume == &context)
                return;
            detach_resume_context(link);
            link.resume          = &context;
            link.resume_previous = nullptr;
            link.resume_next     = context.links;
            if (context.links != nullptr)
                context.links->resume_previous = &link;
            context.links = &link;
        }

        inline resume_context* inherited_resume_context(task_link& link) noexcept {
            if (link.resume != nullptr)
                return link.resume;
            for (auto* parent = link.parent; parent != nullptr; parent = parent->parent) {
                if (parent->resume != nullptr)
                    return parent->resume;
            }
            return nullptr;
        }

        inline void clear_resume_context(resume_context& context) noexcept {
            while (context.links != nullptr)
                detach_resume_context(*context.links);
        }

        inline void resume_task(task_link&              link,
                                std::coroutine_handle<> continuation) noexcept {
            auto* context = inherited_resume_context(link);
            if (context != nullptr && context->active) {
                attach_resume_context(link, *context);
                if (link.parent != nullptr)
                    attach_resume_context(*link.parent, *context);
                context->pending = continuation;
                return;
            }
            resume_context local{};
            context          = &local;
            context->active  = true;
            context->pending = continuation;
            attach_resume_context(link, *context);
            if (link.parent != nullptr)
                attach_resume_context(*link.parent, *context);
            while (context->pending) {
                auto next        = context->pending;
                context->pending = {};
                next.resume();
            }
            context->active = false;
            clear_resume_context(*context);
        }
    } // namespace detail

    template <typename T = void>
    class task;

    template <typename T>
    class task {
        public:
        struct promise_type;
        using handle = std::coroutine_handle<promise_type>;

        struct promise_type {
            task_link link{};
            T         value{};
            error_t   result = error_t::enone;

            static void* operator new(usize size) noexcept {
                return ::operator new(size);
            }
            static void operator delete(void* value) noexcept {
                ::operator delete(value);
            }
            static task get_return_object_on_allocation_failure() noexcept {
                return task(error_t::enomem);
            }
            task get_return_object() {
                return task(handle::from_promise(*this));
            }
            std::suspend_always initial_suspend() const noexcept {
                return {};
            }
            struct final_awaiter {
                bool await_ready() const noexcept {
                    return false;
                }
                std::coroutine_handle<> await_suspend(handle current) const noexcept {
                    auto continuation = current.promise().link.continuation;
                    if (continuation)
                        detail::resume_task(current.promise().link, continuation);
                    return std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            final_awaiter final_suspend() const noexcept {
                return {};
            }
            void return_value(T value_) noexcept {
                value = static_cast<T&&>(value_);
            }
            void return_value(error_t error) noexcept {
                result = error;
            }
            void unhandled_exception() const noexcept {
                __builtin_trap();
            }

            template <typename U>
            auto await_transform(task<U>&& child) noexcept {
                auto result        = static_cast<task<U>&&>(child).operator co_await();
                result.parent_link = &link;
                return result;
            }

            template <typename Awaitable>
            decltype(auto) await_transform(Awaitable&& awaitable) noexcept {
                return static_cast<Awaitable&&>(awaitable);
            }
        };

        task() = default;
        explicit task(handle value) : _coroutine(value) {}
        explicit task(error_t error) : _error(error) {}
        task(const task&)            = delete;
        task& operator=(const task&) = delete;
        task(task&& other) noexcept : _coroutine(other._coroutine), _error(other._error) {
            other._coroutine = {};
            other._error     = error_t::enone;
        }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                reset();
                _coroutine       = other._coroutine;
                _error           = other._error;
                other._coroutine = {};
                other._error     = error_t::enone;
            }
            return *this;
        }
        ~task() {
            reset();
        }

        bool done() const {
            return !_coroutine || _coroutine.done();
        }
        bool valid() const {
            return _coroutine && !_coroutine.done();
        }
        error_t error() const {
            if (_error != error_t::enone)
                return _error;
            return _coroutine ? _coroutine.promise().result : error_t::enone;
        }
        bool failed() const {
            return error() != error_t::enone;
        }
        T* value() {
            return _coroutine ? &_coroutine.promise().value : nullptr;
        }

        bool resume() {
            if (done())
                return false;
            auto waiting = _coroutine.promise().link.waiting;
            if (waiting && !waiting.done())
                waiting.resume();
            else
                _coroutine.resume();
            return true;
        }

        struct awaiter {
            handle                  coroutine;
            std::coroutine_handle<> parent;
            task_link*              parent_link;
            awaiter(handle value, std::coroutine_handle<> caller,
                    task_link* link) noexcept
                : coroutine(value), parent(caller), parent_link(link) {}
            awaiter(awaiter&& other) noexcept
                : coroutine(other.coroutine), parent(other.parent),
                  parent_link(other.parent_link) {
                other.coroutine   = {};
                other.parent      = {};
                other.parent_link = nullptr;
            }
            awaiter(const awaiter&)            = delete;
            awaiter& operator=(const awaiter&) = delete;
            ~awaiter() {
                destroy_child();
            }
            bool await_ready() const noexcept {
                return !coroutine || coroutine.done();
            }
            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<> caller) noexcept {
                parent = caller;
                if (parent_link)
                    parent_link->waiting = coroutine;
                coroutine.promise().link.parent       = parent_link;
                coroutine.promise().link.continuation = caller;
                detail::resume_task(coroutine.promise().link, coroutine);
                return std::noop_coroutine();
            }
            optional<T> await_resume() noexcept {
                if (!coroutine)
                    return error_t::enomem;
                const error_t error  = coroutine.promise().result;
                optional<T>   result = error == error_t::enone
                                           ? optional<T>(coroutine.promise().value)
                                           : optional<T>(error);
                destroy_child();
                if (parent_link)
                    parent_link->waiting = {};
                return result;
            }
            void destroy_child() noexcept {
                if (!coroutine)
                    return;
                coroutine.promise().link.continuation = {};
                coroutine.promise().link.parent       = nullptr;
                detail::detach_resume_context(coroutine.promise().link);
                coroutine.destroy();
                coroutine = {};
            }
        };
        awaiter operator co_await() && noexcept {
            awaiter value{_coroutine, {}, nullptr};
            _coroutine = {};
            return value;
        }
        void reset() {
            if (_coroutine) {
                cancel_resume(_coroutine);
                detail::detach_resume_context(_coroutine.promise().link);
                _coroutine.destroy();
            }
            _coroutine = {};
        }

        private:
        handle  _coroutine{};
        error_t _error = error_t::enone;
    };

    /// Minimal owning coroutine task. It is deliberately a building block:
    /// scheduling, cancellation and I/O awaiters belong above this type.
    template <>
    class task<void> {
        public:
        static constexpr usize frame_slot_bytes =
            resource_limits::coroutine::frame_slot_bytes;
        static constexpr usize frame_slot_count = resource_limits::coroutine::frame_slots;
        struct promise_type;
        using handle = std::coroutine_handle<promise_type>;

        task() = default;
        explicit task(handle coroutine) : _coroutine(coroutine) {}
        explicit task(error_t error) : _error(error) {}
        task(const task&)            = delete;
        task& operator=(const task&) = delete;

        task(task&& other) noexcept : _coroutine(other._coroutine), _error(other._error) {
            other._coroutine = {};
            other._error     = error_t::enone;
        }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                reset();
                _coroutine       = other._coroutine;
                _error           = other._error;
                other._coroutine = {};
                other._error     = error_t::enone;
            }
            return *this;
        }

        ~task() {
            reset();
        }

        bool valid() const {
            return _coroutine && !_coroutine.done();
        }
        bool done() const {
            return !_coroutine || _coroutine.done();
        }
        bool failed() const {
            return error() != error_t::enone;
        }
        error_t error() const {
            if (_error != error_t::enone)
                return _error;
            return _coroutine ? _coroutine.promise().result : error_t::enone;
        }

        /// Resume one step. The caller owns scheduling and must not resume a
        /// task concurrently from multiple CPUs.
        bool resume() {
            if (done())
                return false;
            auto waiting = _coroutine.promise().link.waiting;
            if (waiting && !waiting.done())
                waiting.resume();
            else
                _coroutine.resume();
            return true;
        }

        struct awaiter {
            handle                  coroutine;
            std::coroutine_handle<> parent;
            task_link*              parent_link;

            awaiter(handle value, std::coroutine_handle<> caller,
                    task_link* link) noexcept
                : coroutine(value), parent(caller), parent_link(link) {}
            awaiter(awaiter&& other) noexcept
                : coroutine(other.coroutine), parent(other.parent),
                  parent_link(other.parent_link) {
                other.coroutine   = {};
                other.parent      = {};
                other.parent_link = nullptr;
            }
            awaiter(const awaiter&)            = delete;
            awaiter& operator=(const awaiter&) = delete;
            ~awaiter() {
                destroy_child();
            }

            bool await_ready() const noexcept {
                return !coroutine || coroutine.done();
            }
            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<> caller) noexcept {
                parent = caller;
                if (parent_link)
                    parent_link->waiting = coroutine;
                coroutine.promise().link.parent       = parent_link;
                coroutine.promise().link.continuation = caller;
                detail::resume_task(coroutine.promise().link, coroutine);
                return std::noop_coroutine();
            }
            error_t await_resume() noexcept {
                if (!coroutine)
                    return error_t::enomem;
                const error_t result = coroutine.promise().result;
                destroy_child();
                if (parent_link)
                    parent_link->waiting = {};
                return result;
            }
            void destroy_child() noexcept {
                if (!coroutine)
                    return;
                coroutine.promise().link.continuation = {};
                coroutine.promise().link.parent       = nullptr;
                detail::detach_resume_context(coroutine.promise().link);
                coroutine.destroy();
                coroutine = {};
            }
        };

        awaiter operator co_await() && noexcept {
            awaiter result{_coroutine, {}, nullptr};
            _coroutine = {};
            return result;
        }

        void reset() {
            if (_coroutine) {
                cancel_resume(_coroutine);
                detail::detach_resume_context(_coroutine.promise().link);
                _coroutine.destroy();
            }
            _coroutine = {};
        }

        struct promise_type {
            task_link link{};

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
            std::suspend_always initial_suspend() const noexcept {
                return {};
            }
            struct final_awaiter {
                bool await_ready() const noexcept {
                    return false;
                }
                std::coroutine_handle<> await_suspend(handle current) const noexcept {
                    auto continuation = current.promise().link.continuation;
                    if (continuation)
                        detail::resume_task(current.promise().link, continuation);
                    return std::noop_coroutine();
                }
                void await_resume() const noexcept {}
            };
            final_awaiter final_suspend() const noexcept {
                return {};
            }
            void return_value(error_t error) noexcept {
                result = error;
            }
            void unhandled_exception() const noexcept {
                __builtin_trap();
            }
            error_t result = error_t::enone;

            template <typename U>
            auto await_transform(task<U>&& child) noexcept {
                auto result        = static_cast<task<U>&&>(child).operator co_await();
                result.parent_link = &link;
                return result;
            }

            template <typename Awaitable>
            decltype(auto) await_transform(Awaitable&& awaitable) noexcept {
                return static_cast<Awaitable&&>(awaitable);
            }
        };

        private:
        struct alignas(16) frame_slot {
            u8  storage[frame_slot_bytes - sizeof(u32)];
            u32 next;
        };

        inline static frame_slot _frame_pool[frame_slot_count]{};
        inline static u32        _frame_head        = 0;
        inline static u32        _frame_initialized = 0;
        inline static u32        _pool_allocations  = 0;

        static void initialize_pool() {
            u32 state = __atomic_load_n(&_frame_initialized, __ATOMIC_ACQUIRE);
            if (state == 2)
                return;
            u32 expected = 0;
            if (__atomic_compare_exchange_n(&_frame_initialized, &expected, 1, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                for (u32 i = 0; i + 1 < frame_slot_count; ++i)
                    _frame_pool[i].next = i + 1;
                _frame_pool[frame_slot_count - 1].next = frame_slot_count;
                __atomic_store_n(&_frame_head, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&_frame_initialized, 2, __ATOMIC_RELEASE);
            }
            else {
                while (__atomic_load_n(&_frame_initialized, __ATOMIC_ACQUIRE) != 2) {
                }
            }
        }

        static void* allocate_frame(usize size) noexcept {
            if (size > frame_slot_bytes)
                return nullptr;
            initialize_pool();
            u32 head = __atomic_load_n(&_frame_head, __ATOMIC_ACQUIRE);
            while (head != frame_slot_count) {
                u32 next = _frame_pool[head].next;
                if (__atomic_compare_exchange_n(&_frame_head, &head, next, false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    __atomic_add_fetch(&_pool_allocations, 1, __ATOMIC_RELAXED);
                    return _frame_pool[head].storage;
                }
            }
            return nullptr;
        }

        static bool owns_frame(void* value, u32& index) {
            const usize address = reinterpret_cast<usize>(value);
            const usize first   = reinterpret_cast<usize>(_frame_pool[0].storage);
            const usize last =
                reinterpret_cast<usize>(_frame_pool[frame_slot_count - 1].storage) +
                sizeof(_frame_pool[frame_slot_count - 1].storage);
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
            if (!owns_frame(value, index))
                return;
            u32 head = __atomic_load_n(&_frame_head, __ATOMIC_RELAXED);
            do {
                _frame_pool[index].next = head;
            } while (!__atomic_compare_exchange_n(&_frame_head, &head, index, false,
                                                  __ATOMIC_RELEASE, __ATOMIC_RELAXED));
        }

        public:
        static u32 pooled_frame_allocations() {
            return __atomic_load_n(&_pool_allocations, __ATOMIC_RELAXED);
        }

        private:
        handle  _coroutine{};
        error_t _error = error_t::enone;
    };

    struct yield_awaitable {
        bool await_ready() const noexcept {
            return false;
        }
        void await_suspend(task<>::handle) const noexcept {}
        void await_resume() const noexcept {}
    };

    inline yield_awaitable yield() noexcept {
        return {};
    }

} // namespace astia::core
