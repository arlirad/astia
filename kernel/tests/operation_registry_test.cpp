#include <astia/core/operation_registry.hpp>
#include <astia/core/spinlock.hpp>

#include "support/test.hpp"

namespace {
    struct probe {
        volatile astia::u32        refs        = 0;
        int                        exits       = 0;
        astia::core::cancel_reason last_reason = astia::core::cancel_reason::none;
    };

    int refs_zero_count = 0;

    void probe_exit(void* object, astia::core::cancel_reason reason) {
        auto* value = static_cast<probe*>(object);
        ++value->exits;
        value->last_reason = reason;
    }

    void probe_refs_zero(void*) {
        ++refs_zero_count;
    }

    int destroyed = 0;

    void destroy_probe(void* object) {
        static_cast<probe*>(object)->exits = 100;
        ++destroyed;
    }
}

ASTIA_TEST(operation_registry_registers_and_unregisters) {
    probe            value{};
    const astia::u32 before = astia::core::operation_registry_count();

    ASTIA_CHECK(astia::core::register_operation(&value, 1, 2, &value.refs, &probe_exit));
    ASTIA_CHECK(astia::core::operation_registry_count() == before + 1);
    ASTIA_CHECK(!astia::core::register_operation(&value, 1, 2, &value.refs, &probe_exit));

    astia::core::unregister_operation(&value);
    ASTIA_CHECK(astia::core::operation_registry_count() == before);
    astia::core::cancel_operations_for_process(1, astia::core::cancel_reason::owner);
    ASTIA_CHECK(value.exits == 0);
}

ASTIA_TEST(operation_registry_cancels_process_and_thread_once) {
    probe first{};
    probe second{};
    ASTIA_CHECK(
        astia::core::register_operation(&first, 10, 20, &first.refs, &probe_exit));
    ASTIA_CHECK(
        astia::core::register_operation(&second, 10, 30, &second.refs, &probe_exit));

    astia::core::cancel_operations_for_thread(10, 20, astia::core::cancel_reason::owner);
    ASTIA_CHECK(first.exits == 1);
    ASTIA_CHECK(first.last_reason == astia::core::cancel_reason::owner);
    ASTIA_CHECK(second.exits == 0);

    astia::core::cancel_operations_for_thread(10, 20,
                                              astia::core::cancel_reason::request);
    ASTIA_CHECK(first.exits == 1);

    astia::core::cancel_operations_for_process(10, astia::core::cancel_reason::shutdown);
    ASTIA_CHECK(second.exits == 1);
    ASTIA_CHECK(second.last_reason == astia::core::cancel_reason::shutdown);

    astia::core::cancel_operations_for_process(10, astia::core::cancel_reason::request);
    ASTIA_CHECK(first.exits == 1);
    ASTIA_CHECK(second.exits == 1);

    astia::core::unregister_operation(&first);
    astia::core::unregister_operation(&second);
}

ASTIA_TEST(operation_registry_release_callback_runs_after_refs_zero) {
    probe value{};
    refs_zero_count = 0;
    ASTIA_CHECK(astia::core::register_operation(&value, 20, 21, &value.refs, &probe_exit,
                                                &probe_refs_zero));
    astia::core::cancel_operations_for_process(20, astia::core::cancel_reason::owner);
    ASTIA_CHECK(value.exits == 1);
    ASTIA_CHECK(value.refs == 0);
    ASTIA_CHECK(refs_zero_count == 1);
    astia::core::unregister_operation(&value);
}

ASTIA_TEST(operation_registry_reaps_deferred_destruction) {
    probe first{};
    probe second{};
    destroyed = 0;
    ASTIA_CHECK(astia::core::defer_operation_destroy(&first, &destroy_probe));
    ASTIA_CHECK(astia::core::defer_operation_destroy(&second, &destroy_probe));
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 2);

    astia::core::reap_operations();
    ASTIA_CHECK(destroyed == 2);
    ASTIA_CHECK(astia::core::operation_reaper_pending() == 0);
}

ASTIA_TEST(operation_registry_allocates_unique_lifecycle_ids) {
    const astia::u64 first  = astia::core::allocate_lifecycle_id();
    const astia::u64 second = astia::core::allocate_lifecycle_id();
    ASTIA_CHECK(first != 0);
    ASTIA_CHECK(second != 0);
    ASTIA_CHECK(first != second);
}
