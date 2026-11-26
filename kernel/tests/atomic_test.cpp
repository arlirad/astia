#include <astia/atomic.hpp>

#include "support/test.hpp"

namespace {
    enum class mode : astia::u8 {
        idle,
        running,
        stopped,
    };
} // namespace

ASTIA_TEST(atomic_pointer_load_store_exchange) {
    int first  = 1;
    int second = 2;
    int third  = 3;

    astia::atomic<int*> slot(&first);

    ASTIA_CHECK(slot.load() == &first);
    slot.store(&second, astia::memory_order::release);
    ASTIA_CHECK(slot.load(astia::memory_order::acquire) == &second);
    ASTIA_CHECK(slot.exchange(&third, astia::memory_order::acq_rel) == &second);
    ASTIA_CHECK(slot.load() == &third);
}

ASTIA_TEST(atomic_pointer_compare_exchange_success) {
    int first  = 1;
    int second = 2;

    astia::atomic<int*> slot(&first);
    int*                expected = &first;

    ASTIA_CHECK(slot.compare_exchange_strong(
        expected, &second, astia::memory_order::acq_rel, astia::memory_order::acquire));
    ASTIA_CHECK(slot.load() == &second);
    ASTIA_CHECK(expected == &first);
}

ASTIA_TEST(atomic_pointer_compare_exchange_failure_updates_expected) {
    int first  = 1;
    int second = 2;

    astia::atomic<int*> slot(&first);
    int*                expected = &second;

    ASTIA_CHECK(!slot.compare_exchange_strong(
        expected, &second, astia::memory_order::acq_rel, astia::memory_order::acquire));
    ASTIA_CHECK(slot.load() == &first);
    ASTIA_CHECK(expected == &first);
}

ASTIA_TEST(atomic_pointer_ref_compare_exchange) {
    int  first  = 1;
    int  second = 2;
    int* slot   = &first;

    astia::atomic_ref<int*> ref(slot);
    int*                    expected = &first;

    ASTIA_CHECK(ref.compare_exchange_strong(
        expected, &second, astia::memory_order::acq_rel, astia::memory_order::acquire));
    ASTIA_CHECK(slot == &second);
    ASTIA_CHECK(expected == &first);
}

ASTIA_TEST(atomic_weak_compare_exchange_retries) {
    astia::atomic<astia::u64> value(0);
    astia::u64                expected = 0;
    astia::u32                attempts = 0;

    while (!value.compare_exchange_weak(expected, 7, astia::memory_order::relaxed,
                                        astia::memory_order::relaxed)) {
        if (++attempts > 1000)
            break;
        expected = 0;
    }

    ASTIA_CHECK(attempts < 1000);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 7);
}

ASTIA_TEST(atomic_compare_exchange_failure_updates_expected) {
    astia::atomic<astia::u64> value(42);
    astia::u64                expected = 7;

    ASTIA_CHECK(!value.compare_exchange_strong(expected, 9, astia::memory_order::acq_rel,
                                               astia::memory_order::acquire));
    ASTIA_CHECK(expected == 42);
    ASTIA_CHECK(value.load() == 42);

    ASTIA_CHECK(value.compare_exchange_strong(expected, 9, astia::memory_order::acq_rel,
                                              astia::memory_order::acquire));
    ASTIA_CHECK(expected == 42);
    ASTIA_CHECK(value.load() == 9);
}

ASTIA_TEST(atomic_enum_storage) {
    astia::atomic<mode> value(mode::idle);

    ASTIA_CHECK(value.load() == mode::idle);
    value.store(mode::running);
    ASTIA_CHECK(value.load() == mode::running);

    mode expected = mode::stopped;
    ASTIA_CHECK(!value.compare_exchange_strong(expected, mode::idle));
    ASTIA_CHECK(expected == mode::running);
    ASTIA_CHECK(value.compare_exchange_strong(expected, mode::idle));
    ASTIA_CHECK(value.load() == mode::idle);
}

ASTIA_TEST(atomic_bool_storage) {
    astia::atomic<bool> flag(false);

    ASTIA_CHECK(!flag.load());
    flag.store(true);
    ASTIA_CHECK(flag.load());
    ASTIA_CHECK(flag.exchange(false));
    ASTIA_CHECK(!flag.load());
}

ASTIA_TEST(atomic_u64_storage) {
    constexpr astia::u64 high = 0x100000000ull;

    astia::atomic<astia::u64> value(high);

    ASTIA_CHECK(value.load() == high);
    value.store(high + 1);
    ASTIA_CHECK(value.load() == high + 1);
    ASTIA_CHECK(value.exchange(high + 2) == high + 1);
    ASTIA_CHECK(value.load() == high + 2);
}

ASTIA_TEST(atomic_fetch_operations_return_previous_value) {
    astia::atomic<astia::u32> value(0x0f0fu);

    ASTIA_CHECK(value.fetch_add(1, astia::memory_order::relaxed) == 0x0f0fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0f10u);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.fetch_sub(1, astia::memory_order::relaxed) == 0x0f0fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0f0eu);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.fetch_or(0x00f0u, astia::memory_order::relaxed) == 0x0f0fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0fffu);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.fetch_and(0x00ffu, astia::memory_order::relaxed) == 0x0f0fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x000fu);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.fetch_xor(0x00ffu, astia::memory_order::relaxed) == 0x0f0fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0ff0u);
}

ASTIA_TEST(atomic_fetch_operations_return_new_value) {
    astia::atomic<astia::u32> value(10);

    ASTIA_CHECK(value.add_fetch(5, astia::memory_order::relaxed) == 15);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 15);

    value.store(10);
    ASTIA_CHECK(value.sub_fetch(5, astia::memory_order::relaxed) == 5);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 5);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.or_fetch(0x00f0u, astia::memory_order::relaxed) == 0x0fffu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0fffu);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.and_fetch(0x00ffu, astia::memory_order::relaxed) == 0x000fu);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x000fu);

    value.store(0x0f0fu);
    ASTIA_CHECK(value.xor_fetch(0x00ffu, astia::memory_order::relaxed) == 0x0ff0u);
    ASTIA_CHECK(value.load(astia::memory_order::relaxed) == 0x0ff0u);
}

ASTIA_TEST(atomic_ref_return_values) {
    astia::u32                    raw = 10;
    astia::atomic_ref<astia::u32> value(raw);

    ASTIA_CHECK(value.fetch_add(5) == 10);
    ASTIA_CHECK(raw == 15);
    ASTIA_CHECK(value.add_fetch(5) == 20);
    ASTIA_CHECK(raw == 20);
    ASTIA_CHECK(value.fetch_sub(5) == 20);
    ASTIA_CHECK(raw == 15);
    ASTIA_CHECK(value.sub_fetch(5) == 10);
    ASTIA_CHECK(raw == 10);
}

ASTIA_TEST(atomic_thread_fence_covers_every_order) {
    astia::atomic_thread_fence(astia::memory_order::relaxed);
    astia::atomic_thread_fence(astia::memory_order::acquire);
    astia::atomic_thread_fence(astia::memory_order::release);
    astia::atomic_thread_fence(astia::memory_order::acq_rel);
    astia::atomic_thread_fence(astia::memory_order::seq_cst);
}
