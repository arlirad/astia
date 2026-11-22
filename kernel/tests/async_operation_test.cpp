#include <astia/core/completion.hpp>
#include <astia/core/operation_registry.hpp>
#include <astia/core/task.hpp>

#include "async_operation.hpp"
#include "support/resume_queue_host.hpp"
#include "support/test.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {
    astia::core::completion<astia::i64>* source_completion = nullptr;
    astia::sys::async_operation*         active_operation  = nullptr;
    int                                  body_entries      = 0;

    astia::core::task<astia::i64> immediate_body(astia::sys::syscall_context& context) {
        ++body_entries;
        co_return context.arguments[0] + static_cast<astia::i64>(context.frame.a1) +
            static_cast<astia::i64>(context.operation.value) +
            static_cast<astia::i64>(context.user_data.value);
    }

    astia::core::task<astia::i64> parked_body(astia::sys::syscall_context& context) {
        ++body_entries;
        const auto result = co_await *source_completion;
        if (!result)
            co_return result.error;
        co_return result.value + context.arguments[0] +
            static_cast<astia::i64>(context.frame.a1);
    }

    astia::core::task<astia::i64> cancels_while_resuming(astia::sys::syscall_context&) {
        ++body_entries;
        const auto result = co_await *source_completion;
        if (!result)
            co_return result.error;
        (void) active_operation->request_cancel();
        co_return result.value + 1;
    }

    astia::core::task<astia::i64>
    identity_after_suspend(astia::sys::syscall_context& context) {
        ++body_entries;
        const auto result = co_await *source_completion;
        if (!result)
            co_return result.error;
        co_return static_cast<astia::i64>(context.operation.value +
                                          context.user_data.value) +
            result.value;
    }

    astia::core::task<astia::i64> failing_body(astia::sys::syscall_context&) {
        ++body_entries;
        co_return astia::error_t::eio;
    }

    astia::u64 create_exit_process = 0;

    void create_owner_exit_hook(astia::u64 process, astia::u64 thread) {
        (void) thread;
        if (process == create_exit_process)
            astia::core::cancel_operations_for_process(process,
                                                       astia::core::cancel_reason::owner);
    }

    std::atomic<bool> body_resume_entered{false};
    std::atomic<bool> body_resume_release{false};

    astia::core::task<astia::i64> body_resume_spinner(astia::sys::syscall_context&) {
        const auto result = co_await *source_completion;
        body_resume_entered.store(true, std::memory_order_release);
        while (!body_resume_release.load(std::memory_order_acquire))
            std::this_thread::yield();
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    std::atomic<bool> start_entered{false};
    std::atomic<bool> start_release{false};

    astia::core::task<astia::i64> start_spinner(astia::sys::syscall_context&) {
        start_entered.store(true, std::memory_order_release);
        while (!start_release.load(std::memory_order_acquire))
            std::this_thread::yield();
        co_return 5;
    }

    struct registry_probe {
        volatile astia::u32 refs = 0;
    };

    void ignore_owner_exit(void*, astia::core::cancel_reason) {}

    struct reaper_probe {
        int* destroyed = nullptr;
    };

    void destroy_reaper_probe(void* object) {
        ++*static_cast<reaper_probe*>(object)->destroyed;
    }

    std::atomic<bool> finish_entered{false};
    std::atomic<bool> finish_release{false};
    std::atomic<int>  finish_wakes{0};

    astia::core::task<astia::i64>
    finish_waiter(astia::core::completion<astia::i64>& value) {
        const auto result = co_await value;
        finish_entered.store(true, std::memory_order_release);
        while (!finish_release.load(std::memory_order_acquire))
            std::this_thread::yield();
        finish_wakes.fetch_add(1, std::memory_order_acq_rel);
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    void fill_resume_queue() {
        const auto noop       = std::noop_coroutine();
        astia::u64 generation = 1;
        while (astia::core::schedule_resume(noop, generation))
            ++generation;
    }

    void clear_resume_queue() {
        const auto noop = std::noop_coroutine();
        astia::core::cancel_resume(noop, 0);
        while (astia::core::drain_resumes(astia::resource_limits::queue::resume * 2 +
                                          1) != 0) {
        }
    }

    astia::core::task<astia::i64>
    await_result(astia::core::completion<astia::i64>& operation, int& wakes) {
        const auto result = co_await operation;
        ++wakes;
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    astia::sys::async_operation*
    make_operation(astia::sys::async_operation::body_factory factory,
                   astia::sys::syscall_id            id, astia::i64 (&arguments)[6],
                   const astia::arch::syscall_frame& frame,
                   astia::sys::operation_id          operation = {},
                   astia::sys::syscall_user_data     user_data = {},
                   astia::sys::async_operation_owner owner     = {}) {
        return astia::sys::async_operation::create(factory, id, arguments, frame,
                                                   operation, user_data, owner);
    }
}

ASTIA_TEST(async_operation_owns_frame_context_and_identity) {
    astia::arch::syscall_frame frame{};
    frame.a1 = 31;
    astia::i64 arguments[astia::sys::syscall_argument_count]{2, 0, 0, 0, 0, 0};
    body_entries = 0;

    auto* operation =
        make_operation(immediate_body, static_cast<astia::sys::syscall_id>(130),
                       arguments, frame, {0x1234}, {0x5678});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    frame.a1     = 99;
    arguments[0] = 50;

    ASTIA_CHECK(&operation->context().frame != &frame);
    ASTIA_CHECK(operation->context().frame.a1 == 31);
    ASTIA_CHECK(operation->context().arguments[0] == 2);
    ASTIA_CHECK(operation->id().value == 0x1234);
    ASTIA_CHECK(operation->user_data().value == 0x5678);
    ASTIA_CHECK(!operation->owner().valid());
    ASTIA_CHECK(!operation->registered());
    ASTIA_CHECK(operation->state() == astia::sys::async_operation_state::created);

    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::completed);
    ASTIA_CHECK(body_entries == 1);
    ASTIA_CHECK(operation->terminal());
    ASTIA_CHECK(operation->result().has_value);
    ASTIA_CHECK(operation->result().value == 2 + 31 + 0x1234 + 0x5678);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::completed);
    ASTIA_CHECK(body_entries == 1);
    operation->destroy();
}

ASTIA_TEST(async_operation_parks_then_completes_once) {
    astia::arch::syscall_frame frame{};
    frame.a1 = 5;
    astia::i64 arguments[astia::sys::syscall_argument_count]{7, 0, 0, 0, 0, 0};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(131), arguments, frame);
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    ASTIA_CHECK(operation->parked());
    ASTIA_CHECK(body_entries == 1);
    ASTIA_CHECK(!operation->completion().done());

    ASTIA_CHECK(source.complete(30));
    ASTIA_CHECK(operation->terminal());
    ASTIA_CHECK(operation->result().has_value);
    ASTIA_CHECK(operation->result().value == 42);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(!source.complete(31));
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::completed);
    ASTIA_CHECK(body_entries == 1);
    operation->destroy();
}

ASTIA_TEST(async_operation_resumes_owner_only_at_terminal_completion) {
    astia::core::initialize_resume_queues();
    astia::arch::test::clear_reschedule_request();

    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(132), arguments, frame);
    int  wakes  = 0;
    auto waiter = await_result(operation->completion(), wakes);
    ASTIA_CHECK(waiter.resume());
    ASTIA_CHECK(!waiter.done());

    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    astia::arch::test::clear_reschedule_request();
    ASTIA_CHECK(source.complete(11));
    ASTIA_CHECK(astia::arch::test::reschedule_requested());
    ASTIA_CHECK(!waiter.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(waiter.done());
    ASTIA_CHECK(waiter.value() != nullptr && *waiter.value() == 11);
    ASTIA_CHECK(wakes == 1);
    operation->destroy();
}

ASTIA_TEST(async_operation_cancels_parked_body_exactly_once) {
    astia::core::initialize_resume_queues();
    astia::arch::test::clear_reschedule_request();

    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(133), arguments, frame);
    int  wakes  = 0;
    auto waiter = await_result(operation->completion(), wakes);
    ASTIA_CHECK(waiter.resume());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

    ASTIA_CHECK(operation->request_cancel(astia::core::cancel_reason::request));
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(operation->reason() == astia::core::cancel_reason::request);
    ASTIA_CHECK(operation->result().error == astia::error_t::eagain);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(!operation->request_cancel());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(waiter.done());
    ASTIA_CHECK(waiter.error() == astia::error_t::eagain);
    ASTIA_CHECK(wakes == 1);

    ASTIA_CHECK(source.complete(3));
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::canceled);
    operation->destroy();
}

ASTIA_TEST(async_operation_cancel_during_wake_is_terminal_exactly_once) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation =
        make_operation(cancels_while_resuming, static_cast<astia::sys::syscall_id>(140),
                       arguments, frame);
    active_operation = operation;
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

    ASTIA_CHECK(source.complete(8));
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(operation->reason() == astia::core::cancel_reason::request);
    ASTIA_CHECK(operation->result().error == astia::error_t::eagain);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(!source.complete(9));
    ASTIA_CHECK(!operation->request_cancel());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::canceled);
    active_operation = nullptr;
    operation->destroy();
}

ASTIA_TEST(async_operation_owner_exit_waits_for_attached_awaiter_detach) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion           = &source;
    body_entries                = 0;
    const astia::u64 process_id = 0x6500;
    const astia::u64 thread_id  = 0x6501;

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(145), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    int  wakes  = 0;
    auto waiter = await_result(operation->completion(), wakes);
    ASTIA_CHECK(waiter.resume());
    ASTIA_CHECK(!waiter.done());

    astia::core::cancel_operations_for_process(process_id,
                                               astia::core::cancel_reason::owner);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(waiter.done());
    ASTIA_CHECK(waiter.error() == astia::error_t::eagain);
    ASTIA_CHECK(wakes == 1);
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_create_owner_exit_keeps_creator_reference) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame frame{};
    astia::i64                 arguments[astia::sys::syscall_argument_count]{};
    const astia::u64           process_id = 0x6700;
    const astia::u64           thread_id  = 0x6701;

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(149), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    astia::core::cancel_operations_for_process(process_id,
                                               astia::core::cancel_reason::owner);
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::canceled);
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_destroy_vs_cancel_with_reaper_pressure_is_safe) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    int          destroyed = 0;
    reaper_probe probes[astia::resource_limits::queue::operation_reaper];
    for (astia::u32 i = 0; i < astia::resource_limits::queue::operation_reaper; ++i) {
        probes[i].destroyed = &destroyed;
        ASTIA_CHECK(
            astia::core::defer_operation_destroy(&probes[i], &destroy_reaper_probe));
    }

    for (astia::u32 iteration = 0; iteration < 100; ++iteration) {
        astia::arch::syscall_frame frame{};
        astia::i64                 arguments[astia::sys::syscall_argument_count]{};
        astia::core::completion<astia::i64> source;
        source_completion           = &source;
        const astia::u64 process_id = 0x7000 + iteration;
        const astia::u64 thread_id  = 0x8000 + iteration;

        auto* operation =
            make_operation(parked_body, static_cast<astia::sys::syscall_id>(150),
                           arguments, frame, {}, {}, {process_id, thread_id});
        if (operation == nullptr) {
            ASTIA_CHECK(operation != nullptr);
            return;
        }
        ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

        std::thread canceller([process_id, thread_id] {
            astia::core::cancel_operations_for_thread(process_id, thread_id,
                                                      astia::core::cancel_reason::owner);
        });
        std::thread destroyer([operation] { operation->destroy(); });
        canceller.join();
        destroyer.join();
        ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    }

    astia::core::reap_operations();
    ASTIA_CHECK(destroyed ==
                static_cast<int>(astia::resource_limits::queue::operation_reaper));
}

ASTIA_TEST(async_operation_create_owner_exit_before_publication_is_safe) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion   = &source;
    create_exit_process = 0xF000;
    astia::sys::async_operation::set_create_hook(&create_owner_exit_hook);

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(155), arguments,
                       frame, {}, {}, {create_exit_process, 0xF001});
    astia::sys::async_operation::set_create_hook(nullptr);

    ASTIA_CHECK(operation == nullptr);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);
}

ASTIA_TEST(async_operation_body_frame_gates_destroy_reap) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_resume_entered.store(false, std::memory_order_release);
    body_resume_release.store(false, std::memory_order_release);
    const astia::u64 process_id = 0xD000;
    const astia::u64 thread_id  = 0xD001;

    auto* operation =
        make_operation(body_resume_spinner, static_cast<astia::sys::syscall_id>(153),
                       arguments, frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    fill_resume_queue();

    std::thread producer([&source] { ASTIA_CHECK(source.complete_deferred(7)); });
    while (!body_resume_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    operation->destroy();
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    body_resume_release.store(true, std::memory_order_release);
    producer.join();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    clear_resume_queue();
}

ASTIA_TEST(async_operation_body_frame_gates_owner_exit_reap) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_resume_entered.store(false, std::memory_order_release);
    body_resume_release.store(false, std::memory_order_release);
    const astia::u64 process_id = 0xE000;
    const astia::u64 thread_id  = 0xE001;

    auto* operation =
        make_operation(body_resume_spinner, static_cast<astia::sys::syscall_id>(154),
                       arguments, frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    fill_resume_queue();

    std::thread producer([&source] { ASTIA_CHECK(source.complete_deferred(8)); });
    while (!body_resume_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    astia::core::cancel_operations_for_process(process_id,
                                               astia::core::cancel_reason::owner);
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    body_resume_release.store(true, std::memory_order_release);
    producer.join();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    clear_resume_queue();
}

ASTIA_TEST(async_operation_reap_vs_destroy_is_serialized) {
    astia::core::reap_operations();
    for (astia::u32 iteration = 0; iteration < 150; ++iteration) {
        astia::arch::syscall_frame frame{};
        astia::i64                 arguments[astia::sys::syscall_argument_count]{};
        astia::core::completion<astia::i64> source;
        source_completion           = &source;
        const astia::u64 process_id = 0x9000 + iteration;
        const astia::u64 thread_id  = 0xA000 + iteration;

        auto* operation =
            make_operation(parked_body, static_cast<astia::sys::syscall_id>(151),
                           arguments, frame, {}, {}, {process_id, thread_id});
        if (operation == nullptr) {
            ASTIA_CHECK(operation != nullptr);
            return;
        }
        ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

        std::atomic<bool> destroy_done{false};
        std::thread       destroyer([operation, &destroy_done] {
            operation->destroy();
            destroy_done.store(true, std::memory_order_release);
        });
        std::thread       reaper([&destroy_done] {
            while (!destroy_done.load(std::memory_order_acquire))
                astia::core::reap_operations();
        });
        destroyer.join();
        reaper.join();
        astia::core::reap_operations();
        ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    }
}

ASTIA_TEST(async_operation_reap_vs_owner_exit_is_serialized) {
    astia::core::reap_operations();
    for (astia::u32 iteration = 0; iteration < 150; ++iteration) {
        astia::arch::syscall_frame frame{};
        astia::i64                 arguments[astia::sys::syscall_argument_count]{};
        astia::core::completion<astia::i64> source;
        source_completion           = &source;
        const astia::u64 process_id = 0xB000 + iteration;
        const astia::u64 thread_id  = 0xC000 + iteration;

        auto* operation =
            make_operation(parked_body, static_cast<astia::sys::syscall_id>(152),
                           arguments, frame, {}, {}, {process_id, thread_id});
        if (operation == nullptr) {
            ASTIA_CHECK(operation != nullptr);
            return;
        }
        ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

        std::atomic<bool> cancel_done{false};
        std::thread       canceller([process_id, thread_id, &cancel_done] {
            astia::core::cancel_operations_for_thread(process_id, thread_id,
                                                      astia::core::cancel_reason::owner);
            cancel_done.store(true, std::memory_order_release);
        });
        std::thread       reaper([&cancel_done] {
            while (!cancel_done.load(std::memory_order_acquire))
                astia::core::reap_operations();
        });
        canceller.join();
        operation->destroy();
        cancel_done.store(true, std::memory_order_release);
        reaper.join();
        astia::core::reap_operations();
        ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    }
}

ASTIA_TEST(async_operation_start_teardown_race_is_serialized) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame frame{};
    astia::i64                 arguments[astia::sys::syscall_argument_count]{};
    start_entered.store(false, std::memory_order_release);
    start_release.store(false, std::memory_order_release);
    const astia::u64 process_id = 0x6600;
    const astia::u64 thread_id  = 0x6601;

    auto* operation =
        make_operation(start_spinner, static_cast<astia::sys::syscall_id>(146), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    std::atomic<int> start_result{0};
    std::thread      starter([&operation, &start_result] {
        start_result.store(static_cast<int>(operation->start()),
                           std::memory_order_release);
    });
    while (!start_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    ASTIA_CHECK(operation->request_cancel());
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    start_release.store(true, std::memory_order_release);
    starter.join();
    ASTIA_CHECK(static_cast<astia::sys::async_start_result>(
                    start_result.load(std::memory_order_acquire)) ==
                astia::sys::async_start_result::canceled);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_registry_capacity_create_is_clean) {
    astia::core::reap_operations();
    const astia::u32            capacity = astia::core::operation_registry_capacity();
    std::vector<registry_probe> probes(capacity);
    for (astia::u32 i = 0; i < capacity; ++i) {
        ASTIA_CHECK(astia::core::register_operation(&probes[i], 0x8000 + i, 0,
                                                    &probes[i].refs, &ignore_owner_exit));
    }
    ASTIA_CHECK(astia::core::operation_registry_count() == capacity);

    astia::arch::syscall_frame frame{};
    astia::i64                 arguments[astia::sys::syscall_argument_count]{};
    auto*                      operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(147), arguments,
                       frame, {}, {}, {0x8999, 1});
    ASTIA_CHECK(operation == nullptr);
    ASTIA_CHECK(astia::core::operation_registry_count() == capacity);
    for (astia::u32 i = 0; i < capacity; ++i)
        astia::core::unregister_operation(&probes[i]);
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_reaper_capacity_failure_is_explicit) {
    astia::core::reap_operations();
    int          destroyed = 0;
    reaper_probe probes[astia::resource_limits::queue::operation_reaper];
    for (astia::u32 i = 0; i < astia::resource_limits::queue::operation_reaper; ++i) {
        probes[i].destroyed = &destroyed;
        ASTIA_CHECK(
            astia::core::defer_operation_destroy(&probes[i], &destroy_reaper_probe));
    }
    ASTIA_CHECK(!astia::core::defer_operation_destroy(&probes[0], &destroy_reaper_probe));

    astia::arch::syscall_frame frame{};
    astia::i64                 arguments[astia::sys::syscall_argument_count]{};
    body_entries = 0;
    auto* operation =
        make_operation(failing_body, static_cast<astia::sys::syscall_id>(148), arguments,
                       frame, {}, {}, {0x8A00, 1});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::completed);
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);

    astia::core::reap_operations();
    ASTIA_CHECK(destroyed ==
                static_cast<int>(astia::resource_limits::queue::operation_reaper));
}

ASTIA_TEST(async_operation_shutdown_destroy_during_driver_resume_is_quiescent) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;
    finish_entered.store(false, std::memory_order_release);
    finish_release.store(false, std::memory_order_release);
    finish_wakes.store(0, std::memory_order_release);
    const astia::u64 process_id = 0x6000;
    const astia::u64 thread_id  = 0x6001;

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(143), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    auto waiter = finish_waiter(operation->completion());
    ASTIA_CHECK(waiter.resume());
    fill_resume_queue();

    std::thread producer([&source] { ASTIA_CHECK(source.complete(7)); });
    while (!finish_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    ASTIA_CHECK(operation->state() == astia::sys::async_operation_state::completed);
    operation->shutdown();
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    finish_release.store(true, std::memory_order_release);
    producer.join();
    ASTIA_CHECK(waiter.done());
    ASTIA_CHECK(finish_wakes.load(std::memory_order_acquire) == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    clear_resume_queue();
}

ASTIA_TEST(async_operation_owner_exit_during_driver_resume_is_quiescent) {
    astia::core::initialize_resume_queues();
    astia::core::reap_operations();
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;
    finish_entered.store(false, std::memory_order_release);
    finish_release.store(false, std::memory_order_release);
    finish_wakes.store(0, std::memory_order_release);
    const astia::u64 process_id = 0x7000;
    const astia::u64 thread_id  = 0x7001;

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(144), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    auto waiter = finish_waiter(operation->completion());
    ASTIA_CHECK(waiter.resume());
    fill_resume_queue();

    std::thread producer([&source] { ASTIA_CHECK(source.complete(8)); });
    while (!finish_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    astia::core::cancel_operations_for_process(process_id,
                                               astia::core::cancel_reason::owner);
    ASTIA_CHECK(astia::core::operation_registry_count() == 1);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);

    finish_release.store(true, std::memory_order_release);
    producer.join();
    ASTIA_CHECK(waiter.done());
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
    clear_resume_queue();
}

ASTIA_TEST(async_operation_process_exit_cancels_registered_operation) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;
    astia::core::reap_operations();
    const astia::u64 process_id = 0x1000;
    const astia::u64 thread_id  = 0x2000;

    auto* operation =
        make_operation(parked_body, static_cast<astia::sys::syscall_id>(134), arguments,
                       frame, {}, {}, {process_id, thread_id});
    if (operation == nullptr) {
        ASTIA_CHECK(operation != nullptr);
        return;
    }
    ASTIA_CHECK(operation->registered());
    ASTIA_CHECK(operation->owner().valid());
    ASTIA_CHECK(operation->owner().process == process_id);
    ASTIA_CHECK(operation->owner().thread == thread_id);
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);

    astia::core::cancel_operations_for_thread(process_id, thread_id,
                                              astia::core::cancel_reason::owner);
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(operation->reason() == astia::core::cancel_reason::owner);
    ASTIA_CHECK(operation->result().error == astia::error_t::eagain);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(operation->completion().await_resume().error == astia::error_t::eagain);
    ASTIA_CHECK(source.complete(1));
    operation->destroy();
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 1);
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_thread_exit_cancels_only_matching_thread) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* first  = make_operation(parked_body, static_cast<astia::sys::syscall_id>(135),
                                  arguments, frame, {}, {}, {0x3000, 0x4000});
    auto* second = make_operation(parked_body, static_cast<astia::sys::syscall_id>(136),
                                  arguments, frame, {}, {}, {0x3000, 0x5000});
    if (first == nullptr || second == nullptr) {
        ASTIA_CHECK(first != nullptr && second != nullptr);
        return;
    }
    ASTIA_CHECK(first->start() == astia::sys::async_start_result::parked);
    ASTIA_CHECK(second->state() == astia::sys::async_operation_state::created);

    astia::core::cancel_operations_for_thread(0x3000, 0x4000,
                                              astia::core::cancel_reason::owner);
    ASTIA_CHECK(first->canceled());
    ASTIA_CHECK(second->state() == astia::sys::async_operation_state::created);

    astia::core::cancel_operations_for_process(0x3000, astia::core::cancel_reason::owner);
    ASTIA_CHECK(second->canceled());
    (void) source.complete(2);
    first->destroy();
    second->destroy();
    astia::core::reap_operations();
    ASTIA_CHECK(astia::core::operation_registry_count() == 0);
}

ASTIA_TEST(async_operation_destruction_tears_down_parked_body) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(137), arguments, frame);
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    operation->destroy();
    ASTIA_CHECK(source.complete(9));
}

ASTIA_TEST(async_operation_cancel_before_start_does_not_run_body) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(138), arguments, frame);
    ASTIA_CHECK(operation->request_cancel());
    ASTIA_CHECK(operation->canceled());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::canceled);
    ASTIA_CHECK(body_entries == 0);
    operation->destroy();
}

ASTIA_TEST(async_operation_delivers_synchronous_error_once) {
    astia::arch::syscall_frame frame{};
    astia::i64                 arguments[astia::sys::syscall_argument_count]{};
    body_entries = 0;

    auto* operation = make_operation(
        failing_body, static_cast<astia::sys::syscall_id>(139), arguments, frame);
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::completed);
    ASTIA_CHECK(operation->terminal());
    ASTIA_CHECK(!operation->result().has_value);
    ASTIA_CHECK(operation->result().error == astia::error_t::eio);
    ASTIA_CHECK(operation->completion().done());
    ASTIA_CHECK(!operation->request_cancel());

    int  wakes  = 0;
    auto waiter = await_result(operation->completion(), wakes);
    ASTIA_CHECK(waiter.resume());
    ASTIA_CHECK(waiter.done());
    ASTIA_CHECK(waiter.error() == astia::error_t::eio);
    ASTIA_CHECK(wakes == 1);
    operation->destroy();
}

ASTIA_TEST(async_operation_preserves_identity_across_suspension) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation =
        make_operation(identity_after_suspend, static_cast<astia::sys::syscall_id>(141),
                       arguments, frame, {0x22}, {0x33});
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    ASTIA_CHECK(operation->id().value == 0x22);
    ASTIA_CHECK(operation->user_data().value == 0x33);
    ASTIA_CHECK(source.complete(1));
    ASTIA_CHECK(operation->result().value == 1 + 0x22 + 0x33);
    operation->destroy();
}

ASTIA_TEST(async_operation_without_owner_ignores_owner_exit) {
    astia::arch::syscall_frame          frame{};
    astia::i64                          arguments[astia::sys::syscall_argument_count]{};
    astia::core::completion<astia::i64> source;
    source_completion = &source;
    body_entries      = 0;

    auto* operation = make_operation(
        parked_body, static_cast<astia::sys::syscall_id>(142), arguments, frame);
    ASTIA_CHECK(!operation->registered());
    ASTIA_CHECK(operation->start() == astia::sys::async_start_result::parked);
    astia::core::cancel_operations_for_process(0, astia::core::cancel_reason::owner);
    ASTIA_CHECK(operation->parked());
    ASTIA_CHECK(operation->request_cancel());
    ASTIA_CHECK(operation->canceled());
    operation->destroy();
}
