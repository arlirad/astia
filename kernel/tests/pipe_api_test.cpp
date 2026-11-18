#include "../src/ipc/pipe.hpp"
#include "support/test.hpp"

namespace {

    using expected_pipe_result = astia::optional<astia::fs::pipe_endpoints>;

    template <typename T>
    struct is_expected_pipe_result {
        static constexpr bool value = false;
    };

    template <>
    struct is_expected_pipe_result<expected_pipe_result> {
        static constexpr bool value = true;
    };

    struct ownership {
        int* live = nullptr;

        ownership() = default;
        explicit ownership(int* count) : live(count) {
            ++*live;
        }
        ownership(const ownership& other) : live(other.live) {
            if (live != nullptr)
                ++*live;
        }
        ownership(ownership&& other) noexcept : live(other.live) {
            other.live = nullptr;
        }
        ownership& operator=(ownership&& other) noexcept {
            if (this != &other) {
                release();
                live       = other.live;
                other.live = nullptr;
            }
            return *this;
        }
        ~ownership() {
            release();
        }

        void release() {
            if (live != nullptr) {
                --*live;
                live = nullptr;
            }
        }
    };

} // namespace

static_assert(is_expected_pipe_result<decltype(astia::fs::create_pipe())>::value);

ASTIA_TEST(pipe_result_failure_preserves_error_without_owned_values) {
    int                                                 live = 0;
    astia::optional<astia::tuple<ownership, ownership>> result(astia::error_t::enomem);

    ASTIA_CHECK(!result);
    ASTIA_CHECK(result.error == astia::error_t::enomem);
    ASTIA_CHECK(live == 0);
}

ASTIA_TEST(tuple_result_moves_both_owned_values) {
    int live = 0;
    {
        astia::optional<astia::tuple<ownership, ownership>> result(
            astia::tuple<ownership, ownership>{ownership(&live), ownership(&live)});
        ASTIA_CHECK(result);
        ASTIA_CHECK(live == 2);

        auto first  = static_cast<ownership&&>(result.value.first);
        auto second = static_cast<ownership&&>(result.value.second);
        ASTIA_CHECK(first.live == &live);
        ASTIA_CHECK(second.live == &live);
        ASTIA_CHECK(live == 2);
    }
    ASTIA_CHECK(live == 0);
}
