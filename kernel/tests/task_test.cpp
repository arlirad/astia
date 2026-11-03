#include "support/test.hpp"
#include <astia/core/task.hpp>

namespace {
    astia::core::task three_steps(int& steps) {
        ++steps;
        co_await astia::core::yield();
        ++steps;
        co_await astia::core::yield();
        ++steps;
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
    astia::core::task moved(static_cast<astia::core::task&&>(source));
    ASTIA_CHECK(!source.valid());
    ASTIA_CHECK(moved.resume());
    ASTIA_CHECK(steps == 1);
    ASTIA_CHECK(!moved.done());
}
