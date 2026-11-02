#include "arena.hpp"
#include "support/test.hpp"
#include <astia/core/rcu_hashmap.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace {
    alignas(16) astia::u8 heap_region[64 * 1024];
    bool heap_ready = false;

    void ensure_heap() {
        if (heap_ready)
            return;
        astia_test::map_arena();
        astia::mm::memory_map map;
        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(heap_region, sizeof heap_region,
                                   [](astia::u64, astia::u64) { return true; });
        ASTIA_CHECK(astia::mm::heap::init());
        heap_ready = true;
    }

    astia::u64 hash_key(const int& key) {
        return static_cast<astia::u64>(static_cast<astia::u32>(key) * 2654435761u);
    }

    bool equal_key(const int& lhs, const int& rhs) {
        return lhs == rhs;
    }
}

ASTIA_TEST(rcu_hashmap_publishes_and_erases_snapshots) {
    ensure_heap();
    astia::core::rcu_hashmap<int, int, 4> map(hash_key, equal_key);
    ASTIA_CHECK(map.insert_or_assign(7, 42));
    auto value = map.find(7);
    ASTIA_CHECK(value && value.value == 42);
    ASTIA_CHECK(map.insert_or_assign(7, 99));
    value = map.find(7);
    ASTIA_CHECK(value && value.value == 99);
    ASTIA_CHECK(map.erase(7));
    ASTIA_CHECK(!map.find(7));
}

ASTIA_TEST(rcu_hashmap_supports_concurrent_readers_and_writers) {
    ensure_heap();
    astia::core::rcu_hashmap<int, int, 8, 64> map(hash_key, equal_key);
    constexpr int                             keys = 32;
    for (int key = 0; key < keys; ++key)
        ASTIA_CHECK(map.insert_or_assign(key, 0));

    std::atomic<bool>        failed{false};
    std::atomic<bool>        start{false};
    std::vector<std::thread> workers;
    for (int writer = 0; writer < 4; ++writer) {
        workers.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int round = 1; round <= 500; ++round) {
                const int key = (round + writer * 7) % keys;
                if (!map.insert_or_assign(key, round))
                    failed.store(true, std::memory_order_release);
            }
        });
    }
    for (int reader = 0; reader < 8; ++reader) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int round = 0; round < 4000; ++round) {
                const int key   = round % keys;
                auto      value = map.find(key);
                if (value && value.value < 0)
                    failed.store(true, std::memory_order_release);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers)
        worker.join();

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
    for (int key = 0; key < keys; ++key)
        ASTIA_CHECK(map.find(key));
}
