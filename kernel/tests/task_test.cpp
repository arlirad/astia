#include "support/test.hpp"
#include "support/resume_queue_host.hpp"
#include <astia/core/task.hpp>
#include <astia/core/async_operation.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/resource_limits.hpp>

namespace astia::proc {
    struct process {};
}

namespace {
    struct custom_value {
        int value;
    };

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

    astia::core::task<custom_value> custom_task(int value) {
        co_return custom_value{value};
    }

    astia::core::task<int> awaits_custom_task() {
        auto result = co_await custom_task(41);
        if (!result)
            co_return result.error;
        co_return result.value.value + 1;
    }

    astia::core::task<custom_value> awaits_int_task() {
        auto result = co_await value_task();
        if (!result)
            co_return result.error;
        co_return custom_value{result.value + 1};
    }

    using process_ptr = astia::mm::shptr<astia::proc::process>;

    astia::core::task<astia::i64> read_for_spawn(
        astia::core::async_operation<astia::i64>& operation) {
        auto result = co_await operation;
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    astia::core::task<bool> map_for_spawn(
        astia::core::async_operation<astia::i64>& operation) {
        auto result = co_await read_for_spawn(operation);
        if (!result)
            co_return result.error;
        co_return result.value == 4096;
    }

    astia::core::task<process_ptr> spawn_for_test(
        astia::core::async_operation<astia::i64>& operation,
        astia::proc::process* process) {
        auto result = co_await map_for_spawn(operation);
        if (!result)
            co_return result.error;
        if (!result.value)
            co_return astia::error_t::eio;
        co_return process_ptr::borrowed(process);
    }

    astia::core::task<astia::i64> drive_spawn_for_test(
        astia::core::async_operation<astia::i64>& operation,
        astia::proc::process* process) {
        auto result = co_await spawn_for_test(operation, process);
        if (!result)
            co_return result.error;
        co_return result.value ? 1 : 0;
    }

    astia::core::task<int> waits_for_deferred_operation(
        astia::core::async_operation<int>& operation, int& resumed,
        int* order = nullptr, astia::usize index = 0) {
        (void) co_await operation;
        ++resumed;
        if (order != nullptr)
            order[index] = resumed - 1;
        co_return resumed;
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

ASTIA_TEST(typed_task_reports_allocation_failure) {
    auto task = astia::core::task<int>::promise_type::get_return_object_on_allocation_failure();
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::enomem);
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

ASTIA_TEST(task_links_are_type_erased) {
    auto int_task = awaits_custom_task();
    while (!int_task.done())
        ASTIA_CHECK(int_task.resume());
    ASTIA_CHECK(int_task.error() == astia::error_t::enone);
    ASTIA_CHECK(int_task.value() != nullptr && *int_task.value() == 42);

    auto custom_task = awaits_int_task();
    while (!custom_task.done())
        ASTIA_CHECK(custom_task.resume());
    ASTIA_CHECK(custom_task.error() == astia::error_t::enone);
    ASTIA_CHECK(custom_task.value() != nullptr && custom_task.value()->value == 43);
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

ASTIA_TEST(task_cross_type_chain_transfers_once) {
    astia::core::async_operation<astia::i64> operation;
    astia::proc::process process;
    auto task = drive_spawn_for_test(operation, &process);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(operation.complete(4096));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.error() == astia::error_t::enone);
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 1);
}

ASTIA_TEST(async_operation_deferred_queue_overflow_is_lossless) {
    constexpr astia::usize count = 300;
    astia::core::async_operation<int> operations[count];
    astia::core::task<int> tasks[count];
    int order[count];
    int resumed = 0;

    for (astia::usize i = 0; i < count; ++i) {
        order[i] = -1;
        tasks[i] = waits_for_deferred_operation(operations[i], resumed, order, i);
        ASTIA_CHECK(tasks[i].resume());
    }
    for (astia::usize i = 0; i < count; ++i)
        ASTIA_CHECK(operations[i].complete_deferred(static_cast<int>(i)));

    ASTIA_CHECK(astia::core::drain_resumes(static_cast<astia::u32>(count)) == count);
    ASTIA_CHECK(resumed == static_cast<int>(count));
    for (astia::usize i = 0; i < count; ++i) {
        ASTIA_CHECK(tasks[i].done());
        ASTIA_CHECK(order[i] == static_cast<int>(i));
    }
}

ASTIA_TEST(async_operation_deferred_completion_is_canceled_before_destroy) {
    astia::core::async_operation<int> operation;
    int resumed = 0;
    auto task = waits_for_deferred_operation(operation, resumed);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(operation.complete_deferred(7));
    task.reset();
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
    ASTIA_CHECK(resumed == 0);
}

ASTIA_TEST(async_operation_deferred_completion_requests_scheduler_resume) {
    astia::core::async_operation<int> operation;
    int resumed = 0;
    auto task = waits_for_deferred_operation(operation, resumed);

    ASTIA_CHECK(task.resume());
    astia::arch::test::clear_reschedule_request();
    ASTIA_CHECK(operation.complete_deferred(7));
    ASTIA_CHECK(astia::arch::test::reschedule_requested());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(resumed == 1);
}

ASTIA_TEST(resume_queue_rejects_stale_generation) {
    astia::core::initialize_resume_queues();
    const auto continuation = std::noop_coroutine();
    ASTIA_CHECK(astia::core::schedule_resume(continuation, 11));
    astia::core::cancel_resume(continuation, 10);
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(astia::core::schedule_resume(continuation, 12));
    astia::core::cancel_resume(continuation, 12);
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
}

ASTIA_TEST(resume_queue_saturation_is_bounded) {
    astia::core::initialize_resume_queues();
    const auto continuation = std::noop_coroutine();
    astia::u32 accepted = 0;
    for (;;) {
        if (!astia::core::schedule_resume(continuation, accepted + 1))
            break;
        ++accepted;
    }
    ASTIA_CHECK(accepted != 0);
    astia::u32 drained = 0;
    while (const astia::u32 count = astia::core::drain_resumes(
               astia::resource_limits::queue::resume * 2 + 1))
        drained += count;
    ASTIA_CHECK(drained == accepted);
    ASTIA_CHECK(astia::core::schedule_resume(continuation, accepted + 1));
    astia::core::cancel_resume(continuation, accepted + 1);
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
}
