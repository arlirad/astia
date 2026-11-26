#include <astia/core/mpmc_queue.hpp>

#include "support/test.hpp"

#include <atomic>
#include <thread>
#include <vector>

ASTIA_TEST(mpmc_queue_is_bounded) {
    astia::core::mpmc_queue<int, 4> queue;

    ASTIA_CHECK(queue.capacity() == 4);
    ASTIA_CHECK(queue.try_push(1));
    ASTIA_CHECK(queue.try_push(2));
    ASTIA_CHECK(queue.try_push(3));
    ASTIA_CHECK(queue.try_push(4));
    ASTIA_CHECK(!queue.try_push(5));
    ASTIA_CHECK(queue.full_approx());

    int value = 0;

    ASTIA_CHECK(queue.try_pop(value) && value == 1);
    ASTIA_CHECK(queue.try_pop(value) && value == 2);
    ASTIA_CHECK(queue.try_pop(value) && value == 3);
    ASTIA_CHECK(queue.try_pop(value) && value == 4);
    ASTIA_CHECK(!queue.try_pop(value));
    ASTIA_CHECK(queue.empty_approx());
}

ASTIA_TEST(mpmc_queue_supports_concurrent_producers_and_consumers) {
    constexpr int producers        = 4;
    constexpr int consumers        = 4;
    constexpr int items_per_worker = 4000;
    constexpr int total            = producers * items_per_worker;

    astia::core::mpmc_queue<int, 64> queue;
    std::vector<std::atomic<int>>    seen(total);

    for (auto& value : seen) {
        value.store(0);
    }

    std::atomic<int> finished_producers{0};
    std::atomic<int> consumed{0};
    std::atomic<int> invalid{0};

    std::vector<std::thread> workers;

    for (int producer = 0; producer < producers; ++producer) {
        workers.emplace_back([&, producer] {
            for (int item = 0; item < items_per_worker; ++item) {
                const int value = producer * items_per_worker + item;

                while (!queue.try_push(value)) {
                    std::this_thread::yield();
                }
            }

            finished_producers.fetch_add(1);
        });
    }

    for (int consumer = 0; consumer < consumers; ++consumer) {
        workers.emplace_back([&] {
            int value = 0;

            while (true) {
                if (queue.try_pop(value)) {
                    if (value < 0 || value >= total)
                        invalid.fetch_add(1);
                    else
                        seen[static_cast<astia::usize>(value)].fetch_add(1);

                    consumed.fetch_add(1);
                    continue;
                }

                if (finished_producers.load() == producers && consumed.load() == total)
                    return;

                std::this_thread::yield();
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(invalid.load() == 0);
    ASTIA_CHECK(consumed.load() == total);

    for (const auto& value : seen) {
        ASTIA_CHECK(value.load() == 1);
    }

    ASTIA_CHECK(queue.empty_approx());
}
