#include "scheduler_policy.hpp"
#include "test.hpp"

namespace {
    using namespace astia;
    namespace policy = astia::boot::aarch64::virt::sched::policy;

    [[nodiscard]] policy::thread thread_with_state(policy::thread_state state) {
        policy::thread value{};

        value.state = state;

        return value;
    }
} // namespace

ASTIA_TEST(aarch64_scheduler_deadline_policy) {
    u64 due = 0;

    ASTIA_CHECK(!policy::deadline_after(10, 0, due));
    ASTIA_CHECK(!policy::deadline_after(~u64{0}, 1, due));
    ASTIA_CHECK(policy::deadline_after(0, 10, due));
    ASTIA_CHECK(due == 10U);
    ASTIA_CHECK(policy::deadline_after(100, 20, due));
    ASTIA_CHECK(due == 120U);
}

ASTIA_TEST(aarch64_scheduler_sleep_policy) {
    policy::thread value = thread_with_state(policy::thread_state::running);

    ASTIA_CHECK(!policy::sleep_until(value, 0));
    ASTIA_CHECK(policy::sleep_until(value, 30));
    ASTIA_CHECK(value.state == policy::thread_state::sleeping);
    ASTIA_CHECK(value.wake_deadline_ms == 30U);
    ASTIA_CHECK(!policy::sleep_until(value, 40));

    policy::thread idle = thread_with_state(policy::thread_state::free);

    ASTIA_CHECK(!policy::sleep_until(idle, 30));
    ASTIA_CHECK(idle.state == policy::thread_state::free);
}

ASTIA_TEST(aarch64_scheduler_wake_policy) {
    policy::thread value = thread_with_state(policy::thread_state::running);

    ASTIA_CHECK(!policy::wake(value));

    ASTIA_CHECK(policy::sleep_until(value, 30));
    ASTIA_CHECK(policy::wake(value));
    ASTIA_CHECK(value.state == policy::thread_state::runnable);
    ASTIA_CHECK(value.wake_deadline_ms == 0U);
    ASTIA_CHECK(value.wake_count == 1U);
    ASTIA_CHECK(!policy::wake(value));
    ASTIA_CHECK(value.wake_count == 1U);

    policy::thread no_deadline = thread_with_state(policy::thread_state::sleeping);

    ASTIA_CHECK(!policy::wake(no_deadline));
    ASTIA_CHECK(no_deadline.wake_count == 0U);
}

ASTIA_TEST(aarch64_scheduler_wake_due_policy) {
    policy::thread value = thread_with_state(policy::thread_state::running);

    ASTIA_CHECK(policy::sleep_until(value, 30));
    ASTIA_CHECK(!policy::wake_if_due(value, 29));
    ASTIA_CHECK(value.state == policy::thread_state::sleeping);
    ASTIA_CHECK(policy::wake_if_due(value, 30));
    ASTIA_CHECK(value.state == policy::thread_state::runnable);
    ASTIA_CHECK(value.wake_count == 1U);
    ASTIA_CHECK(!policy::wake_if_due(value, 30));

    policy::thread no_deadline = thread_with_state(policy::thread_state::sleeping);

    ASTIA_CHECK(!policy::wake_if_due(no_deadline, 100));
    ASTIA_CHECK(no_deadline.wake_count == 0U);
}

ASTIA_TEST(aarch64_scheduler_select_policy) {
    policy::thread table[policy::max_threads]{};

    table[0].state = policy::thread_state::sleeping;
    table[1].state = policy::thread_state::runnable;
    table[2].state = policy::thread_state::finished;
    table[3].state = policy::thread_state::runnable;

    ASTIA_CHECK(policy::next_runnable(table, policy::max_threads, -1) == 1);
    ASTIA_CHECK(policy::next_runnable(table, policy::max_threads, 1) == 3);
    ASTIA_CHECK(policy::next_runnable(table, policy::max_threads, 3) == 1);

    table[1].state = policy::thread_state::free;
    table[3].state = policy::thread_state::free;

    ASTIA_CHECK(policy::next_runnable(table, policy::max_threads, -1) == -1);
    ASTIA_CHECK(policy::next_runnable(nullptr, policy::max_threads, -1) == -1);
    ASTIA_CHECK(policy::next_runnable(table, 0, -1) == -1);
    ASTIA_CHECK(policy::next_runnable(table, policy::max_threads + 1, -1) == -1);
}

ASTIA_TEST(aarch64_scheduler_state_predicates) {
    ASTIA_CHECK(policy::finished(thread_with_state(policy::thread_state::finished)));
    ASTIA_CHECK(!policy::finished(thread_with_state(policy::thread_state::runnable)));
    ASTIA_CHECK(policy::sleeping(thread_with_state(policy::thread_state::sleeping)));
    ASTIA_CHECK(!policy::sleeping(thread_with_state(policy::thread_state::runnable)));
}
