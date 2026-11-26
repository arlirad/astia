#include <astia/core/rcu_grace.hpp>
#include <astia/core/rcu_hashmap.hpp>
#include <astia/core/rcu_vector.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "arena.hpp"
#include "support/test.hpp"

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

    struct tracked_int {
        int id = -1;

        static std::atomic<int> live[4];

        tracked_int() = default;

        explicit tracked_int(int value) : id(value) {
            retain();
        }

        tracked_int(const tracked_int& other) : id(other.id) {
            retain();
        }

        tracked_int& operator=(const tracked_int& other) {
            if (this != &other) {
                release();

                id = other.id;

                retain();
            }

            return *this;
        }

        ~tracked_int() {
            release();
        }

        private:
        void retain() {
            if (id >= 0 && id < 4)
                live[id].fetch_add(1, std::memory_order_relaxed);
        }

        void release() {
            if (id >= 0 && id < 4)
                live[id].fetch_sub(1, std::memory_order_relaxed);
        }
    };

    std::atomic<int> tracked_int::live[4] = {};

    void reset_tracked() {
        for (auto& count : tracked_int::live) {
            count.store(0, std::memory_order_relaxed);
        }
    }

    int tracked_live(int id) {
        return tracked_int::live[id].load(std::memory_order_relaxed);
    }

    constexpr astia::u64 epoch_max  = ~astia::u64{0};
    constexpr astia::u64 epoch_half = astia::u64{1} << 63;

    std::atomic<bool> reclaim_seam_entered{false};
    std::atomic<bool> reclaim_seam_release{false};
    std::thread::id   reclaim_seam_thread{};

    void reset_reclaim_seam() {
        reclaim_seam_entered.store(false, std::memory_order_release);
        reclaim_seam_release.store(false, std::memory_order_release);

        reclaim_seam_thread = std::thread::id{};
    }

    void wait_for_reclaim_seam() {
        while (!reclaim_seam_entered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void release_reclaim_seam() {
        reclaim_seam_release.store(true, std::memory_order_release);
    }

    void reclaim_seam() {
        if (std::this_thread::get_id() != reclaim_seam_thread)
            return;

        reclaim_seam_entered.store(true, std::memory_order_release);

        while (!reclaim_seam_release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    std::atomic<bool> post_exchange_seen{false};
    std::atomic<bool> reader_check_before_exchange{false};

    void post_exchange_probe() {
        post_exchange_seen.store(true, std::memory_order_release);
    }

    void reader_check_probe() {
        if (!post_exchange_seen.load(std::memory_order_acquire))
            reader_check_before_exchange.store(true, std::memory_order_release);
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

    constexpr int keys = 32;

    for (int key = 0; key < keys; ++key) {
        ASTIA_CHECK(map.insert_or_assign(key, 0));
    }

    std::atomic<bool>        failed{false};
    std::atomic<bool>        start{false};
    std::vector<std::thread> workers;

    for (int writer = 0; writer < 4; ++writer) {
        workers.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 1; round <= 500; ++round) {
                const int key = (round + writer * 7) % keys;

                if (!map.insert_or_assign(key, round))
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    for (int reader = 0; reader < 8; ++reader) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 0; round < 4000; ++round) {
                const int key = round % keys;

                auto value = map.find(key);

                if (value && value.value < 0)
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));

    for (int key = 0; key < keys; ++key) {
        ASTIA_CHECK(map.find(key));
    }
}

ASTIA_TEST(rcu_hashmap_reclamation_grace_stress) {
    ensure_heap();

    constexpr int writers = 8;
    constexpr int readers = 8;
    constexpr int keys    = 8;
    constexpr int rounds  = 5000;

    astia::core::rcu_hashmap<int, int, 1, 64> map(hash_key, equal_key);

    for (int key = 0; key < keys; ++key) {
        ASTIA_CHECK(map.insert_or_assign(key, 0));
    }

    std::atomic<bool>        start{false};
    std::atomic<bool>        failed{false};
    std::atomic<int>         active_writers{writers};
    std::vector<std::thread> workers;

    for (int writer = 0; writer < writers; ++writer) {
        workers.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 1; round <= rounds; ++round) {
                const int key = (round + writer) % keys;

                if (!map.insert_or_assign(key, round))
                    failed.store(true, std::memory_order_release);
            }

            active_writers.fetch_sub(1, std::memory_order_release);
        });
    }

    for (int reader = 0; reader < readers; ++reader) {
        workers.emplace_back([&, reader] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 0; round < rounds * 4; ++round) {
                if (round >= rounds &&
                    active_writers.load(std::memory_order_acquire) == 0)
                    break;

                const int key = reader % keys;

                auto value = map.find(key);

                if (value && value.value < 0)
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));

    for (int key = 0; key < keys; ++key) {
        ASTIA_CHECK(map.find(key));
    }
}

ASTIA_TEST(rcu_grace_defers_current_epoch_retirement) {
    reset_tracked();

    using grace_type = astia::core::rcu_grace_period<tracked_int>;
    grace_type grace;

    auto* old        = new tracked_int(0);
    auto* old_record = new grace_type::retired_record{};

    grace.retire(old, old_record);
    ASTIA_CHECK(tracked_live(0) == 1);

    reset_reclaim_seam();

    astia::core::rcu_test::reclaim_pre_exchange_observer = &reclaim_seam;

    std::thread reclaimer([&] {
        reclaim_seam_thread = std::this_thread::get_id();

        grace.reclaim();
    });

    wait_for_reclaim_seam();

    auto* fresh        = new tracked_int(1);
    auto* fresh_record = new grace_type::retired_record{};

    grace.retire(fresh, fresh_record);

    release_reclaim_seam();
    reclaimer.join();

    astia::core::rcu_test::reclaim_pre_exchange_observer = nullptr;

    ASTIA_CHECK(tracked_live(0) == 0);
    ASTIA_CHECK(tracked_live(1) == 1);

    grace.reclaim();
    ASTIA_CHECK(tracked_live(1) == 0);
}

ASTIA_TEST(rcu_grace_epoch_comparison_boundaries) {
    using astia::core::rcu_detail::epoch_precedes;

    ASTIA_CHECK(epoch_precedes(10, 11));
    ASTIA_CHECK(!epoch_precedes(11, 11));
    ASTIA_CHECK(!epoch_precedes(12, 11));

    ASTIA_CHECK(epoch_precedes(epoch_max, 0));
    ASTIA_CHECK(!epoch_precedes(0, epoch_max));
    ASTIA_CHECK(epoch_precedes(epoch_max - 1, epoch_max));
    ASTIA_CHECK(!epoch_precedes(epoch_max, epoch_max - 1));
    ASTIA_CHECK(!epoch_precedes(epoch_max, epoch_max));

    ASTIA_CHECK(epoch_precedes(1, epoch_half));
    ASTIA_CHECK(epoch_precedes(epoch_half + 1, 0));
    ASTIA_CHECK(!epoch_precedes(0, epoch_half));
    ASTIA_CHECK(!epoch_precedes(epoch_half, 0));
}

ASTIA_TEST(rcu_grace_detaches_before_reader_observation) {
    using grace_type = astia::core::rcu_grace_period<tracked_int>;
    grace_type grace;

    post_exchange_seen.store(false, std::memory_order_release);
    reader_check_before_exchange.store(false, std::memory_order_release);

    astia::core::rcu_test::reclaim_post_exchange_observer = &post_exchange_probe;
    astia::core::rcu_test::reclaim_reader_check_observer  = &reader_check_probe;

    grace.reclaim();

    astia::core::rcu_test::reclaim_post_exchange_observer = nullptr;
    astia::core::rcu_test::reclaim_reader_check_observer  = nullptr;

    ASTIA_CHECK(post_exchange_seen.load(std::memory_order_acquire));
    ASTIA_CHECK(!reader_check_before_exchange.load(std::memory_order_acquire));
}

ASTIA_TEST(rcu_grace_defers_eligible_retirement_for_active_reader) {
    reset_tracked();

    using grace_type = astia::core::rcu_grace_period<tracked_int>;
    grace_type grace;

    auto* value  = new tracked_int(0);
    auto* record = new grace_type::retired_record{};

    grace.enter();
    grace.retire(value, record);
    ASTIA_CHECK(tracked_live(0) == 1);

    grace.reclaim();
    ASTIA_CHECK(grace.epoch_for_test() == 2);
    ASTIA_CHECK(tracked_live(0) == 1);

    grace.leave();
    grace.reclaim();
    ASTIA_CHECK(grace.epoch_for_test() == 3);
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_grace_epoch_wraps_after_uint64_max) {
    reset_tracked();

    using grace_type = astia::core::rcu_grace_period<tracked_int>;
    grace_type grace;

    grace.set_epoch_for_test(epoch_max);

    auto* old        = new tracked_int(0);
    auto* old_record = new grace_type::retired_record{};

    grace.retire(old, old_record);
    ASTIA_CHECK(grace.epoch_for_test() == epoch_max);

    grace.reclaim();
    ASTIA_CHECK(grace.epoch_for_test() == 0);
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_grace_epoch_cutoff_around_wrap) {
    reset_tracked();

    using grace_type = astia::core::rcu_grace_period<tracked_int>;
    grace_type grace;

    grace.set_epoch_for_test(epoch_max);

    auto* old        = new tracked_int(0);
    auto* old_record = new grace_type::retired_record{};

    grace.retire(old, old_record);

    reset_reclaim_seam();

    astia::core::rcu_test::reclaim_pre_exchange_observer = &reclaim_seam;

    std::thread reclaimer([&] {
        reclaim_seam_thread = std::this_thread::get_id();

        grace.reclaim();
    });

    wait_for_reclaim_seam();
    ASTIA_CHECK(grace.epoch_for_test() == 0);

    auto* fresh        = new tracked_int(1);
    auto* fresh_record = new grace_type::retired_record{};

    grace.retire(fresh, fresh_record);

    release_reclaim_seam();
    reclaimer.join();

    astia::core::rcu_test::reclaim_pre_exchange_observer = nullptr;

    ASTIA_CHECK(tracked_live(0) == 0);
    ASTIA_CHECK(tracked_live(1) == 1);

    grace.reclaim();
    ASTIA_CHECK(grace.epoch_for_test() == 1);
    ASTIA_CHECK(tracked_live(1) == 0);
}

// The reclaim seam pauses after the reader observation. The reclaimer has
// already detached its list, so a snapshot retired while the seam is held must
// not be freed by that reclaim.
ASTIA_TEST(rcu_hashmap_reclamation_grace_seam) {
    ensure_heap();

    astia::core::rcu_hashmap<int, int, 2, 64> map(hash_key, equal_key);

    ASTIA_CHECK(map.insert_or_assign(0, 0));

    reset_reclaim_seam();

    astia::core::rcu_test::reclaim_observer = &reclaim_seam;

    std::thread reclaimer([&] {
        reclaim_seam_thread = std::this_thread::get_id();

        (void) map.insert_or_assign(0, 1);
    });

    wait_for_reclaim_seam();

    {
        astia::core::rcu_hashmap<int, int, 2, 64>::read_guard guard(map, 0);

        std::thread writer([&] { (void) map.insert_or_assign(0, 2); });

        writer.join();

        release_reclaim_seam();
        reclaimer.join();

        const int* value = guard.find(0);

        astia::core::rcu_test::reclaim_observer = nullptr;

        ASTIA_CHECK(value != nullptr);

        if (value != nullptr)
            ASTIA_CHECK(*value == 0);
    }
}

// A reader enters while the reclaimer is paused immediately before the retired
// list exchange. The writer then retires the snapshot held by that reader. The
// reclaimer must observe the reader and defer reclamation; the tracked value
// must stay alive until the reader leaves.
ASTIA_TEST(rcu_hashmap_reclamation_grace_exchange_seam) {
    ensure_heap();
    reset_tracked();

    astia::core::rcu_hashmap<int, tracked_int, 1, 64> map(hash_key, equal_key);

    ASTIA_CHECK(map.insert_or_assign(0, tracked_int(0)));
    ASTIA_CHECK(tracked_live(0) == 1);

    reset_reclaim_seam();

    astia::core::rcu_test::reclaim_pre_exchange_observer = &reclaim_seam;

    std::atomic<bool> reclaimer_ok{false};

    std::thread reclaimer([&] {
        reclaim_seam_thread = std::this_thread::get_id();

        reclaimer_ok.store(map.insert_or_assign(0, tracked_int(1)),
                           std::memory_order_release);
    });

    wait_for_reclaim_seam();

    {
        astia::core::rcu_hashmap<int, tracked_int, 1, 64>::read_guard guard(map, 0);

        const tracked_int* value = guard.find(0);

        ASTIA_CHECK(value != nullptr);

        if (value != nullptr)
            ASTIA_CHECK(value->id == 0);

        std::atomic<bool> writer_ok{false};

        std::thread writer([&] {
            writer_ok.store(map.insert_or_assign(0, tracked_int(2)),
                            std::memory_order_release);
        });

        writer.join();

        release_reclaim_seam();
        reclaimer.join();

        astia::core::rcu_test::reclaim_pre_exchange_observer = nullptr;

        ASTIA_CHECK(writer_ok.load(std::memory_order_acquire));
        ASTIA_CHECK(reclaimer_ok.load(std::memory_order_acquire));

        value = guard.find(0);

        ASTIA_CHECK(value != nullptr);

        if (value != nullptr)
            ASTIA_CHECK(value->id == 0);

        ASTIA_CHECK(tracked_live(0) == 1);
    }

    ASTIA_CHECK(map.insert_or_assign(0, tracked_int(3)));
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_hashmap_reclamation_defers_eligible_snapshot) {
    ensure_heap();
    reset_tracked();

    astia::core::rcu_hashmap<int, tracked_int, 1, 64> map(hash_key, equal_key);

    ASTIA_CHECK(map.insert_or_assign(0, tracked_int(0)));
    ASTIA_CHECK(tracked_live(0) == 1);

    {
        astia::core::rcu_hashmap<int, tracked_int, 1, 64>::read_guard guard(map, 0);

        std::atomic<bool> writer_ok{false};

        std::thread writer([&] {
            writer_ok.store(map.insert_or_assign(0, tracked_int(1)),
                            std::memory_order_release);
        });

        writer.join();
        ASTIA_CHECK(writer_ok.load(std::memory_order_acquire));

        ASTIA_CHECK(map.insert_or_assign(0, tracked_int(2)));

        const tracked_int* value = guard.find(0);

        ASTIA_CHECK(value != nullptr);

        if (value != nullptr)
            ASTIA_CHECK(value->id == 0);

        ASTIA_CHECK(tracked_live(0) == 1);
    }

    ASTIA_CHECK(map.insert_or_assign(0, tracked_int(3)));
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_vector_reclamation_grace_stress) {
    constexpr int writers = 8;
    constexpr int readers = 8;
    constexpr int rounds  = 5000;

    astia::core::rcu_vector<int, 8> vector;

    int seed[8] = {0, 1, 2, 3, 4, 5, 6, 0x55aa};

    ASTIA_CHECK(vector.replace(astia::span<const int>(seed, 8)));

    std::atomic<bool>        start{false};
    std::atomic<bool>        failed{false};
    std::atomic<int>         active_writers{writers};
    std::vector<std::thread> workers;

    for (int writer = 0; writer < writers; ++writer) {
        workers.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 1; round <= rounds; ++round) {
                int values[8] = {writer, round, writer ^ round, 0, 0, 0, 0, 0x55aa};

                if (!vector.replace(astia::span<const int>(values, 8)))
                    failed.store(true, std::memory_order_release);
            }

            active_writers.fetch_sub(1, std::memory_order_release);
        });
    }

    for (int reader = 0; reader < readers; ++reader) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 0; round < rounds * 4; ++round) {
                if (round >= rounds &&
                    active_writers.load(std::memory_order_acquire) == 0)
                    break;

                astia::core::rcu_vector<int, 8>::read_guard guard(vector);

                if (guard.size() != 8 || guard[7] != 0x55aa)
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
}

// The vector path uses the same grace period. A reader enters immediately
// before the reclaimer exchanges the retired list, the writer then retires the
// reader's snapshot, and reclamation must defer until the guard leaves.
ASTIA_TEST(rcu_vector_reclamation_grace_exchange_seam) {
    reset_tracked();

    astia::core::rcu_vector<tracked_int, 4> vector;

    {
        tracked_int seed[4] = {tracked_int(0), tracked_int(1), tracked_int(2),
                               tracked_int(3)};

        ASTIA_CHECK(vector.replace(astia::span<const tracked_int>(seed, 4)));
    }

    ASTIA_CHECK(tracked_live(0) == 1);

    reset_reclaim_seam();

    astia::core::rcu_test::reclaim_pre_exchange_observer = &reclaim_seam;

    std::atomic<bool> reclaimer_ok{false};

    std::thread reclaimer([&] {
        reclaim_seam_thread = std::this_thread::get_id();

        tracked_int values[4] = {tracked_int(10), tracked_int(11), tracked_int(12),
                                 tracked_int(13)};

        reclaimer_ok.store(vector.replace(astia::span<const tracked_int>(values, 4)),
                           std::memory_order_release);
    });

    wait_for_reclaim_seam();

    {
        astia::core::rcu_vector<tracked_int, 4>::read_guard guard(vector);
        ASTIA_CHECK(guard.size() == 4);
        ASTIA_CHECK(guard[0].id == 0);

        std::atomic<bool> writer_ok{false};

        std::thread writer([&] {
            tracked_int values[4] = {tracked_int(20), tracked_int(21), tracked_int(22),
                                     tracked_int(23)};

            writer_ok.store(vector.replace(astia::span<const tracked_int>(values, 4)),
                            std::memory_order_release);
        });

        writer.join();

        release_reclaim_seam();
        reclaimer.join();

        astia::core::rcu_test::reclaim_pre_exchange_observer = nullptr;

        ASTIA_CHECK(writer_ok.load(std::memory_order_acquire));
        ASTIA_CHECK(reclaimer_ok.load(std::memory_order_acquire));

        ASTIA_CHECK(guard.size() == 4);
        ASTIA_CHECK(guard[0].id == 0);
        ASTIA_CHECK(tracked_live(0) == 1);
    }

    tracked_int values[4] = {tracked_int(30), tracked_int(31), tracked_int(32),
                             tracked_int(33)};

    ASTIA_CHECK(vector.replace(astia::span<const tracked_int>(values, 4)));
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_vector_reclamation_defers_eligible_snapshot) {
    reset_tracked();

    astia::core::rcu_vector<tracked_int, 4> vector;

    {
        tracked_int seed[4] = {tracked_int(0), tracked_int(1), tracked_int(2),
                               tracked_int(3)};

        ASTIA_CHECK(vector.replace(astia::span<const tracked_int>(seed, 4)));
    }

    ASTIA_CHECK(tracked_live(0) == 1);

    {
        astia::core::rcu_vector<tracked_int, 4>::read_guard guard(vector);

        std::atomic<bool> writer_ok{false};

        std::thread writer([&] {
            tracked_int values[4] = {tracked_int(10), tracked_int(11), tracked_int(12),
                                     tracked_int(13)};

            writer_ok.store(vector.replace(astia::span<const tracked_int>(values, 4)),
                            std::memory_order_release);
        });

        writer.join();
        ASTIA_CHECK(writer_ok.load(std::memory_order_acquire));

        tracked_int values[4] = {tracked_int(20), tracked_int(21), tracked_int(22),
                                 tracked_int(23)};

        ASTIA_CHECK(vector.replace(astia::span<const tracked_int>(values, 4)));
        ASTIA_CHECK(guard.size() == 4);
        ASTIA_CHECK(guard[0].id == 0);
        ASTIA_CHECK(tracked_live(0) == 1);
    }

    tracked_int values[4] = {tracked_int(30), tracked_int(31), tracked_int(32),
                             tracked_int(33)};

    ASTIA_CHECK(vector.replace(astia::span<const tracked_int>(values, 4)));
    ASTIA_CHECK(tracked_live(0) == 0);
}

ASTIA_TEST(rcu_vector_supports_concurrent_cas_writers) {
    astia::core::rcu_vector<int, 16> vector;
    std::atomic<bool>                start{false};
    std::atomic<bool>                failed{false};
    std::vector<std::thread>         workers;

    for (int writer = 0; writer < 4; ++writer) {
        workers.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 1; round <= 500; ++round) {
                int values[4] = {writer, round, writer ^ round, 0x55aa};

                if (!vector.replace(astia::span<const int>(values, 4)))
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    for (int reader = 0; reader < 8; ++reader) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int round = 0; round < 4000; ++round) {
                astia::core::rcu_vector<int, 16>::read_guard guard(vector);

                if (guard.size() != 0 && guard.size() != 4)
                    failed.store(true, std::memory_order_release);

                if (guard.size() == 4 && guard[3] != 0x55aa)
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
}
