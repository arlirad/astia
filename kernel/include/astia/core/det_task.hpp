#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>

#include <coroutine>
#include <new>

namespace astia::core {
    /// Fire-and-forget coroutine: starts eagerly, owns no external handle, and
    /// destroys its own frame when the body returns.  It is the outermost
    /// coroutine of a detached operation: it may `co_await` a `task` and run
    /// cleanup after that task finishes.
    struct det_task {
        /// False when the frame could not be allocated and the body never ran.
        bool started = true;

        /// Run `operation` and hand its result to `continuation` when it
        /// finishes.  The chained det_task starts eagerly and owns both the
        /// task and the continuation across a suspension.
        template <typename T, typename Continuation>
        static det_task then(task<T> operation, Continuation continuation) {
            auto completed = co_await static_cast<task<T>&&>(operation);

            continuation(static_cast<decltype(completed)&&>(completed));
        }

        struct promise_type {
            det_task get_return_object() noexcept {
                return det_task{true};
            }

            static det_task get_return_object_on_allocation_failure() noexcept {
                return det_task{false};
            }

            std::suspend_never initial_suspend() noexcept {
                return {};
            }

            std::suspend_never final_suspend() noexcept {
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {
                __builtin_trap();
            }

            static void* operator new(usize size) noexcept {
                return ::operator new(size);
            }

            static void* operator new(usize size, const std::nothrow_t&) noexcept {
                return ::operator new(size, std::nothrow);
            }

            static void operator delete(void* value) noexcept {
                ::operator delete(value);
            }

            template <typename T>
            auto await_transform(task<T>&& child) noexcept {
                return static_cast<task<T>&&>(child).operator co_await();
            }

            template <typename Awaitable>
            decltype(auto) await_transform(Awaitable&& value) noexcept {
                return static_cast<Awaitable&&>(value);
            }
        };
    };
} // namespace astia::core
