// Pins the waitpid waiter-ownership contract: a waiter detached and reaped
// on another path must not leave the wake path with a dangling raw pointer.
// The shipping waiter_registry is driven directly so the test does not need
// the scheduler or QEMU.

#include <astia/arch/context.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/shptr.hpp>

#include "process.hpp"
#include "support/test.hpp"
#include "thread.hpp"
#include "waiter_registry.hpp"

namespace astia::arch {
    struct context {};

    context* context_create() {
        return nullptr;
    }

    void context_destroy(context*) {}
} // namespace astia::arch

namespace astia::mm::heap {
    void free(void*) {}
} // namespace astia::mm::heap

namespace astia::proc {
    // detach() calls these only for a thread that still has an owning
    // process; this test uses an ownerless record, so the stubs exist only
    // to satisfy the linker.
    mm::shptr<thread> process::take_thread(thread*) {
        return {};
    }

    void process::release_thread_stack(thread*) {}

    void defer_dead_thread(const mm::shptr<thread>&) {}
} // namespace astia::proc

namespace {
    astia::mm::shptr<astia::proc::thread> make_thread(astia::proc::tid_t id) {
        return astia::mm::shptr<astia::proc::thread>(
            new astia::proc::thread(nullptr, id));
    }
} // namespace

ASTIA_TEST(waiter_registry_wake_after_detached_waiter_is_reclaimed) {
    astia::proc::waiter_registry registry;

    auto  process_ref = make_thread(1);
    auto* waiter      = process_ref.get();

    ASTIA_CHECK(registry.add(process_ref));
    ASTIA_CHECK(waiter->detach() == 0);

    waiter->state(astia::proc::thread_state::dead);

    // Detach + exit + reclaim: the process table drops its owning reference.
    // Only the registry reference keeps the record valid for the wake path.
    process_ref = {};

    astia::mm::shptr<astia::proc::thread> wake_list[astia::proc::waiter_capacity];
    const astia::u32 count = registry.drain(wake_list, astia::proc::waiter_capacity);

    ASTIA_CHECK(count == 1);
    ASTIA_CHECK(wake_list[0].get() == waiter);
    ASTIA_CHECK(wake_list[0]->tid() == 1);
    ASTIA_CHECK(!wake_list[0]->wake_wait());
}

ASTIA_TEST(waiter_registry_drain_wakes_each_waiter_once) {
    astia::proc::waiter_registry registry;

    auto first  = make_thread(1);
    auto second = make_thread(2);

    ASTIA_CHECK(registry.add(first));
    ASTIA_CHECK(registry.add(second));

    astia::mm::shptr<astia::proc::thread> wake_list[astia::proc::waiter_capacity];

    ASTIA_CHECK(registry.drain(wake_list, astia::proc::waiter_capacity) == 2);
    ASTIA_CHECK(wake_list[0].get() == first.get());
    ASTIA_CHECK(wake_list[1].get() == second.get());

    ASTIA_CHECK(registry.drain(wake_list, astia::proc::waiter_capacity) == 0);
}

ASTIA_TEST(waiter_registry_remove_unlinks_before_wake) {
    astia::proc::waiter_registry registry;

    auto first  = make_thread(1);
    auto second = make_thread(2);

    ASTIA_CHECK(registry.add(first));
    ASTIA_CHECK(registry.add(second));

    astia::mm::shptr<astia::proc::thread> removed[astia::proc::waiter_capacity];

    ASTIA_CHECK(registry.remove(first.get(), removed, astia::proc::waiter_capacity) == 1);
    ASTIA_CHECK(removed[0].get() == first.get());

    astia::mm::shptr<astia::proc::thread> wake_list[astia::proc::waiter_capacity];

    ASTIA_CHECK(registry.drain(wake_list, astia::proc::waiter_capacity) == 1);
    ASTIA_CHECK(wake_list[0].get() == second.get());
}

ASTIA_TEST(waiter_registry_full_table_refuses_another_waiter) {
    astia::proc::waiter_registry registry;

    astia::mm::shptr<astia::proc::thread> keep[astia::proc::waiter_capacity];

    for (astia::u32 i = 0; i < astia::proc::waiter_capacity; ++i) {
        keep[i] = make_thread(i);

        ASTIA_CHECK(registry.add(keep[i]));
    }

    auto extra = make_thread(astia::proc::waiter_capacity);

    ASTIA_CHECK(!registry.add(extra));
}
