#include "support/test.hpp"
#include <astia/core/task.hpp>
#include <astia/core/async_operation.hpp>

namespace {
    astia::core::task<> three_steps(int& steps) {
        ++steps;
        co_await astia::core::yield();
        ++steps;
        co_await astia::core::yield();
        ++steps;
        co_return astia::error_t::enone;
    }

    astia::core::task<> fails_explicitly() {
        co_return astia::error_t::eio;
    }

    astia::core::task<> nested_task(int& steps) {
        ++steps;
        const auto error = co_await three_steps(steps);
        if (error != astia::error_t::enone)
            co_return error;
        ++steps;
        co_return astia::error_t::enone;
    }

    astia::core::task<int> value_task() {
        co_return 42;
    }

    astia::core::task<int> waits_for_operation(astia::core::async_operation<int>& operation) {
        auto result = co_await operation;
        if (!result)
            co_return result.error;
        co_return result.value;
    }
}

ASTIA_TEST(task_runs_and_suspends_at_yield) {
    int steps = 0;
    auto coroutine = three_steps(steps);

    ASTIA_CHECK(coroutine.valid());
    ASTIA_CHECK(steps == 0);
    ASTIA_CHECK(coroutine.resume());
    ASTIA_CHECK(steps == 1);
    ASTIA_CHECK(!coroutine.done());
    ASTIA_CHECK(coroutine.resume());
    ASTIA_CHECK(steps == 2);
    ASTIA_CHECK(coroutine.resume());
    ASTIA_CHECK(steps == 3);
    ASTIA_CHECK(coroutine.done());
    ASTIA_CHECK(!coroutine.resume());
}

ASTIA_TEST(task_is_move_only_owner) {
    int steps = 0;
    auto source = three_steps(steps);
    astia::core::task<> moved(static_cast<astia::core::task<>&&>(source));
    ASTIA_CHECK(!source.valid());
    ASTIA_CHECK(moved.resume());
    ASTIA_CHECK(steps == 1);
    ASTIA_CHECK(!moved.done());
}

ASTIA_TEST(task_uses_its_frame_pool) {
    const astia::u32 before = astia::core::task<>::pooled_frame_allocations();
    int steps = 0;
    auto task = three_steps(steps);
    ASTIA_CHECK(astia::core::task<>::pooled_frame_allocations() == before + 1);
    task.resume();
    task.reset();
}

ASTIA_TEST(task_reports_frame_allocation_failure) {
    astia::core::task<> tasks[astia::core::task<>::frame_slot_count + 1];
    int steps = 0;
    for (astia::usize i = 0; i < astia::core::task<>::frame_slot_count + 1; ++i)
        tasks[i] = three_steps(steps);
    ASTIA_CHECK(tasks[astia::core::task<>::frame_slot_count].failed());
    ASTIA_CHECK(tasks[astia::core::task<>::frame_slot_count].error() == astia::error_t::enomem);
}

ASTIA_TEST(task_reports_explicit_async_error) {
    auto task = fails_explicitly();
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::eio);
}

ASTIA_TEST(task_supports_nested_await) {
    int steps = 0;
    auto task = nested_task(steps);
    while (!task.done())
        ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(steps == 5);
    ASTIA_CHECK(task.error() == astia::error_t::enone);
}

ASTIA_TEST(task_returns_typed_value) {
    auto task = value_task();
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.error() == astia::error_t::enone);
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 42);
}

ASTIA_TEST(async_operation_resumes_task_without_callback) {
    astia::core::async_operation<int> operation;
    auto task = waits_for_operation(operation);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(operation.complete(77));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.error() == astia::error_t::enone);
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 77);
}
