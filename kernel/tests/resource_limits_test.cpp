#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#include <astia/api/aio.h>
#include <astia/core/mpmc_queue.hpp>
#include <astia/mapping.hpp>
#include <astia/resource_limits.hpp>

ASTIA_TEST(resource_limits_publish_fixed_contracts) {
    ASTIA_CHECK(astia::resource_limits::abi::aio_ring_slots == 16);
    ASTIA_CHECK(astia::resource_limits::abi::aio_ring_entries == 64);
    ASTIA_CHECK(astia::resource_limits::abi::aio_parked_operations == 64);
    ASTIA_CHECK(astia::resource_limits::abi::syscall_table_capacity == 128);
    ASTIA_CHECK(astia::resource_limits::policy::process_count == 64);
    ASTIA_CHECK(astia::resource_limits::policy::threads_per_process == 16);
    ASTIA_CHECK(astia::resource_limits::policy::init_steps == 128);
    ASTIA_CHECK(astia::resource_limits::coroutine::frame_slots == 32);
    ASTIA_CHECK(astia::resource_limits::coroutine::frame_slot_bytes == 2048);
    ASTIA_CHECK(ASTIA_AIO_MAX_RING_SLOTS == astia::resource_limits::abi::aio_ring_slots);
    ASTIA_CHECK(ASTIA_AIO_MAX_RING_ENTRIES ==
                astia::resource_limits::abi::aio_ring_entries);
    ASTIA_CHECK(ASTIA_AIO_MAX_PARKED_OPERATIONS ==
                astia::resource_limits::abi::aio_parked_operations);
}

ASTIA_TEST(resource_limits_queue_accepts_exact_capacity_only) {
    astia::core::mpmc_queue<int, astia::resource_limits::queue::resume> queue;

    for (astia::u32 i = 0; i < astia::resource_limits::queue::resume; ++i) {
        ASTIA_CHECK(queue.try_push(static_cast<int>(i)));
    }

    ASTIA_CHECK(queue.size_approx() == astia::resource_limits::queue::resume);
    ASTIA_CHECK(!queue.try_push(static_cast<int>(astia::resource_limits::queue::resume)));

    int value = -1;

    for (astia::u32 i = 0; i < astia::resource_limits::queue::resume; ++i) {
        ASTIA_CHECK(queue.try_pop(value));
        ASTIA_CHECK(value == static_cast<int>(i));
    }

    ASTIA_CHECK(!queue.try_pop(value));
}

ASTIA_TEST(resource_limit_errors_round_trip_through_abi) {
    ASTIA_CHECK(astia::errno_value(astia::error_t::e2big) == E2BIG);
    ASTIA_CHECK(astia::errno_value(astia::error_t::enfile) == ENFILE);
    ASTIA_CHECK(astia::error_from_errno(E2BIG) == astia::error_t::e2big);
    ASTIA_CHECK(astia::error_from_errno(ENFILE) == astia::error_t::enfile);
}
