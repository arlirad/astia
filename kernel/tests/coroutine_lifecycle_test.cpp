#include <astia/core/completion.hpp>
#include <astia/core/coroutine_lifecycle.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/core/task.hpp>

#include "support/resume_queue_host.hpp"
#include "support/test.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {
    int* probe_destroyed = nullptr;

    struct probe {
        struct promise_type {
            probe get_return_object() {
                return probe{handle::from_promise(*this)};
            }
            std::suspend_always initial_suspend() const noexcept {
                return {};
            }
            std::suspend_always final_suspend() const noexcept {
                return {};
            }
            void return_void() const noexcept {}
            void unhandled_exception() const noexcept {}
            ~promise_type() {
                if (probe_destroyed != nullptr)
                    ++*probe_destroyed;
            }
        };

        using handle = std::coroutine_handle<promise_type>;
        handle coroutine{};
    };

    probe make_probe() {
        co_return;
    }

    void note_observer(void* context) {
        ++*static_cast<int*>(context);
    }

    astia::u32 inline_seen = 0;

    astia::core::task<astia::i64>
    observes_inline_resume(astia::core::completion<astia::i64>& source) {
        const auto result = co_await source;
        inline_seen       = astia::core::coroutine_lifecycle_inflight();
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    astia::core::task<astia::i64>
    observes_deferred_resume(astia::core::completion<astia::i64>& source) {
        const auto result = co_await source;
        inline_seen       = astia::core::coroutine_lifecycle_inflight();
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    std::atomic<bool> fallback_entered{false};
    std::atomic<bool> fallback_release{false};
    std::atomic<int>  fallback_destroyed{0};

    void note_fallback_destroyed(void* context) {
        static_cast<std::atomic<int>*>(context)->fetch_add(1, std::memory_order_acq_rel);
    }

    astia::core::task<astia::i64>
    fallback_spinner(astia::core::completion<astia::i64>& source) {
        const auto result = co_await source;
        fallback_entered.store(true, std::memory_order_release);
        while (!fallback_release.load(std::memory_order_acquire))
            std::this_thread::yield();
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

    astia::core::completion<astia::i64>* outer_source = nullptr;
    astia::core::completion<astia::i64>* inner_source = nullptr;
    astia::core::completion<astia::i64>* inner_result = nullptr;

    astia::core::task<astia::i64> inner_body() {
        const auto result = co_await *inner_source;
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    astia::core::task<astia::i64> outer_body() {
        const auto result = co_await *outer_source;
        if (!result)
            co_return result.error;
        inner_result->complete(result.value + 1);
        co_return result.value;
    }
}

ASTIA_TEST(coroutine_destroy_waits_for_resume_acknowledgement) {
    int destroyed   = 0;
    int observed    = 0;
    probe_destroyed = &destroyed;
    auto value      = make_probe();

    ASTIA_CHECK(astia::core::resume_begin(value.coroutine));
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 1);
    ASTIA_CHECK(
        !astia::core::destroy_coroutine(value.coroutine, note_observer, &observed));
    ASTIA_CHECK(destroyed == 0);
    ASTIA_CHECK(observed == 0);

    astia::core::resume_end(value.coroutine);
    ASTIA_CHECK(destroyed == 1);
    ASTIA_CHECK(observed == 1);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(coroutine_destroy_is_immediate_when_quiescent) {
    int destroyed   = 0;
    int observed    = 0;
    probe_destroyed = &destroyed;
    auto value      = make_probe();

    ASTIA_CHECK(
        astia::core::destroy_coroutine(value.coroutine, note_observer, &observed));
    ASTIA_CHECK(destroyed == 1);
    ASTIA_CHECK(observed == 1);
}

ASTIA_TEST(coroutine_resume_now_balances_lifecycle_refs) {
    int destroyed   = 0;
    probe_destroyed = &destroyed;
    auto value      = make_probe();

    ASTIA_CHECK(astia::core::resume_now(value.coroutine));
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
    ASTIA_CHECK(destroyed == 0);
    ASTIA_CHECK(astia::core::destroy_coroutine(value.coroutine));
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(completion_inline_wake_holds_resume_acknowledgement) {
    astia::core::completion<astia::i64> source;
    inline_seen = 0;
    auto task   = observes_inline_resume(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);

    ASTIA_CHECK(source.complete(9));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 9);
    ASTIA_CHECK(inline_seen != 0);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(completion_deferred_wake_holds_resume_acknowledgement) {
    astia::core::initialize_resume_queues();
    astia::core::completion<astia::i64> source;
    inline_seen = 0;
    auto task   = observes_deferred_resume(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    ASTIA_CHECK(source.complete_deferred(12));
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() != 0);
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 12);
    ASTIA_CHECK(inline_seen != 0);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(completion_reentrant_inline_wake_balances_acknowledgements) {
    astia::core::completion<astia::i64> outer;
    astia::core::completion<astia::i64> inner;
    outer_source = &outer;
    inner_source = &inner;
    inner_result = &inner;

    auto inner_task = inner_body();
    auto outer_task = outer_body();
    ASTIA_CHECK(inner_task.resume());
    ASTIA_CHECK(outer_task.resume());
    ASTIA_CHECK(!inner_task.done());
    ASTIA_CHECK(!outer_task.done());

    ASTIA_CHECK(outer.complete(6));
    ASTIA_CHECK(inner_task.done());
    ASTIA_CHECK(outer_task.done());
    ASTIA_CHECK(inner_task.value() != nullptr && *inner_task.value() == 7);
    ASTIA_CHECK(outer_task.value() != nullptr && *outer_task.value() == 6);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(resume_queue_distinguishes_duplicate_keys_and_generations) {
    astia::core::initialize_resume_queues();
    const auto noop = std::noop_coroutine();

    ASTIA_CHECK(astia::core::resume_begin(noop));
    ASTIA_CHECK(astia::core::schedule_resume_owned(noop, 7, 0x111) ==
                astia::core::resume_schedule::queued);
    ASTIA_CHECK(astia::core::resume_begin(noop));
    ASTIA_CHECK(astia::core::schedule_resume_owned(noop, 7, 0x222) ==
                astia::core::resume_schedule::queued);
    ASTIA_CHECK(astia::core::drain_resumes() == 2);

    ASTIA_CHECK(astia::core::resume_begin(noop));
    ASTIA_CHECK(astia::core::schedule_resume_owned(noop, 8, 0x333) ==
                astia::core::resume_schedule::queued);
    ASTIA_CHECK(astia::core::resume_begin(noop));
    ASTIA_CHECK(astia::core::schedule_resume_owned(noop, 8, 0x333) ==
                astia::core::resume_schedule::duplicate);
    astia::core::resume_end(noop);
    astia::core::cancel_resume(noop, 0, 0);
    while (astia::core::drain_resumes(astia::resource_limits::queue::resume * 2 + 1) !=
           0) {
    }
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(coroutine_lifecycle_capacity_is_explicit_and_released) {
    const astia::u32        capacity = astia::core::coroutine_lifecycle_capacity();
    std::vector<astia::u64> storage((static_cast<astia::usize>(capacity) + 1) * 2, 0);
    auto                    address = [&storage](astia::u32 index) {
        return reinterpret_cast<void*>(reinterpret_cast<astia::usize>(storage.data()) +
                                       static_cast<astia::usize>(index) * 16);
    };

    astia::u32 accepted = 0;
    for (; accepted < capacity; ++accepted) {
        if (!astia::core::resume_begin(
                std::coroutine_handle<>::from_address(address(accepted))))
            break;
    }
    ASTIA_CHECK(accepted == capacity);
    ASTIA_CHECK(!astia::core::resume_begin(
        std::coroutine_handle<>::from_address(address(capacity))));

    for (astia::u32 i = 0; i < accepted; ++i)
        astia::core::resume_end(std::coroutine_handle<>::from_address(address(i)));
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
    ASTIA_CHECK(astia::core::resume_begin(
        std::coroutine_handle<>::from_address(address(capacity))));
    astia::core::resume_end(std::coroutine_handle<>::from_address(address(capacity)));
}

ASTIA_TEST(coroutine_destroy_capacity_exhaustion_is_explicit) {
    const astia::u32        capacity = astia::core::coroutine_lifecycle_capacity();
    std::vector<astia::u64> storage((static_cast<astia::usize>(capacity) + 1) * 2, 0);
    auto                    address = [&storage](astia::u32 index) {
        return reinterpret_cast<void*>(reinterpret_cast<astia::usize>(storage.data()) +
                                       static_cast<astia::usize>(index) * 16);
    };
    for (astia::u32 i = 0; i < capacity; ++i)
        ASTIA_CHECK(
            astia::core::resume_begin(std::coroutine_handle<>::from_address(address(i))));

    int destroyed   = 0;
    probe_destroyed = &destroyed;
    auto value      = make_probe();
    ASTIA_CHECK(!astia::core::destroy_coroutine(value.coroutine));
    ASTIA_CHECK(destroyed == 0);

    astia::core::resume_end(std::coroutine_handle<>::from_address(address(0)));
    ASTIA_CHECK(astia::core::destroy_coroutine(value.coroutine));
    ASTIA_CHECK(destroyed == 1);
    for (astia::u32 i = 1; i < capacity; ++i)
        astia::core::resume_end(std::coroutine_handle<>::from_address(address(i)));
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(completion_inline_detach_before_completion_does_not_resume) {
    astia::core::completion<astia::i64> source;
    inline_seen = 0;
    auto task   = observes_inline_resume(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    task.reset();
    ASTIA_CHECK(source.complete(4));
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
    ASTIA_CHECK(inline_seen == 0);
}

ASTIA_TEST(completion_deferred_detach_before_completion_does_not_resume) {
    astia::core::initialize_resume_queues();
    astia::core::completion<astia::i64> source;
    inline_seen = 0;
    auto task   = observes_deferred_resume(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    task.reset();
    ASTIA_CHECK(source.complete_deferred(4));
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
    ASTIA_CHECK(inline_seen == 0);
}

ASTIA_TEST(completion_overflow_fallback_destroy_race_is_quiescent) {
    astia::core::initialize_resume_queues();
    fill_resume_queue();
    fallback_entered.store(false, std::memory_order_release);
    fallback_release.store(false, std::memory_order_release);
    fallback_destroyed.store(0, std::memory_order_release);

    astia::core::completion<astia::i64> source;
    auto                                task = fallback_spinner(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    std::thread producer([&source] { ASTIA_CHECK(source.complete_deferred(21)); });
    while (!fallback_entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    ASTIA_CHECK(!task.reset_with_observer(&note_fallback_destroyed, &fallback_destroyed));
    ASTIA_CHECK(fallback_destroyed.load(std::memory_order_acquire) == 0);

    fallback_release.store(true, std::memory_order_release);
    producer.join();
    ASTIA_CHECK(fallback_destroyed.load(std::memory_order_acquire) == 1);
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() != 0);

    clear_resume_queue();
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}

ASTIA_TEST(completion_queue_overflow_falls_back_to_acknowledged_inline_wake) {
    astia::core::initialize_resume_queues();
    const auto noop       = std::noop_coroutine();
    astia::u64 generation = 1;
    while (astia::core::schedule_resume(noop, generation))
        ++generation;

    astia::core::completion<astia::i64> source;
    inline_seen = 0;
    auto task   = observes_deferred_resume(source);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    ASTIA_CHECK(source.complete_deferred(15));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 15);
    ASTIA_CHECK(inline_seen != 0);

    astia::core::cancel_resume(noop, 0);
    while (astia::core::drain_resumes(astia::resource_limits::queue::resume * 2 + 1) !=
           0) {
    }
    ASTIA_CHECK(astia::core::coroutine_lifecycle_inflight() == 0);
}
