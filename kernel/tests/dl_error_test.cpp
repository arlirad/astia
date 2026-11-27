#include "error.h"
#include "support/test.hpp"

#include <array>
#include <barrier>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {
    std::string take_text(std::uint64_t thread_key) {
        const char* text = ld_error_take(thread_key);

        return text != nullptr ? std::string(text) : std::string();
    }
} // namespace

ASTIA_TEST(dl_error_reads_and_clears_one_message) {
    ASTIA_CHECK(ld_error_take(1) == nullptr);

    ld_error_set(1, "first failure");

    ASTIA_CHECK(take_text(1) == "first failure");
    ASTIA_CHECK(ld_error_take(1) == nullptr);
}

ASTIA_TEST(dl_error_path_message_appends_prefix_and_path) {
    ld_error_set_path(2, "dlsym: symbol not found: ", "missing_symbol");

    ASTIA_CHECK(take_text(2) == "dlsym: symbol not found: missing_symbol");
    ASTIA_CHECK(ld_error_take(2) == nullptr);
}

ASTIA_TEST(dl_error_message_is_bounded) {
    const std::string long_text(LD_ERROR_TEXT_MAX * 2, 'x');

    ld_error_set_path(3, "dlopen: ", long_text.c_str());

    const std::string message = take_text(3);

    ASTIA_CHECK(message.size() == LD_ERROR_TEXT_MAX - 1);
    ASTIA_CHECK(message.compare(0, 8, "dlopen: ") == 0);
    ASTIA_CHECK(message.find_first_not_of('x', 8) == std::string::npos);
    ASTIA_CHECK(ld_error_take(3) == nullptr);
}

ASTIA_TEST(dl_error_without_a_thread_key_uses_the_shared_buffer) {
    ld_error_set(0, "no thread identity");

    ASTIA_CHECK(take_text(0) == "no thread identity");
    ASTIA_CHECK(ld_error_take(0) == nullptr);
}

ASTIA_TEST(dl_error_messages_are_isolated_across_threads) {
    constexpr int thread_count = 8;

    std::array<std::string, thread_count> observed;
    std::array<int, thread_count>         cleared{};
    std::barrier                          recorded(thread_count);
    std::vector<std::thread>              workers;

    workers.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i) {
        workers.emplace_back([i, &observed, &cleared, &recorded] {
            const std::uint64_t thread_key = 10 + static_cast<std::uint64_t>(i);
            const std::string   message    = "thread " + std::to_string(i);

            ld_error_set(thread_key, message.c_str());
            recorded.arrive_and_wait();

            observed[static_cast<std::size_t>(i)] = take_text(thread_key);
            cleared[static_cast<std::size_t>(i)]  = ld_error_take(thread_key) == nullptr;
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    for (int i = 0; i < thread_count; ++i) {
        const std::string message = "thread " + std::to_string(i);

        ASTIA_CHECK(observed[static_cast<std::size_t>(i)] == message);
        ASTIA_CHECK(cleared[static_cast<std::size_t>(i)] == 1);
    }
}

ASTIA_TEST(dl_error_exhaustion_keeps_slotted_threads_intact) {
    ld_error_set(50, "sentinel");

    for (std::uint64_t candidate = 100; candidate < 100 + LD_ERROR_THREAD_SLOTS + 4;
         ++candidate) {
        ld_error_set(candidate, "overflow");

        ASTIA_CHECK(take_text(candidate) == "overflow");
    }

    ASTIA_CHECK(take_text(50) == "sentinel");
    ASTIA_CHECK(ld_error_take(50) == nullptr);
}
