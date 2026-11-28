#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../src/proc/rlimit_policy.hpp"
#include "support/test.hpp"

namespace {
    using astia::proc::evaluate_rlimit;
    using astia::proc::rlimit_policy_result;
    using astia::proc::rlimit_policy_state;

    constexpr astia::u64 infinity = ~astia::u64{0};

    rlimit_policy_state state(astia::u64 soft, astia::u64 hard) {
        return {astia_rlimit{soft, hard}, astia_rlimit{soft, hard}, 0, 1024};
    }

    bool accepted(const rlimit_policy_result& result, astia::u64 soft, astia::u64 hard) {
        return result && result.value.current == soft && result.value.maximum == hard;
    }
} // namespace

ASTIA_TEST(rlimit_privileged_may_raise_nofile_hard_limit) {
    const auto current = state(32, 32);

    ASTIA_CHECK(accepted(
        evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{64, 64}, current, true), 64, 64));
    ASTIA_CHECK(accepted(
        evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{32, 1024}, current, true), 32, 1024));
}

ASTIA_TEST(rlimit_unprivileged_may_not_raise_nofile_hard_limit) {
    const auto current = state(32, 32);

    ASTIA_CHECK(
        evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{64, 64}, current, false).error ==
        -EPERM);
    ASTIA_CHECK(accepted(
        evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{16, 32}, current, false), 16, 32));
}

ASTIA_TEST(rlimit_privileged_may_raise_address_space_hard_limit) {
    const auto current = state(32, 32);

    ASTIA_CHECK(accepted(evaluate_rlimit(RLIMIT_AS, astia_rlimit{64, 64}, current, true),
                         64, 64));
    ASTIA_CHECK(accepted(
        evaluate_rlimit(RLIMIT_AS, astia_rlimit{infinity, infinity}, current, true),
        infinity, infinity));
}

ASTIA_TEST(rlimit_unprivileged_may_not_raise_address_space_hard_limit) {
    const auto current = state(32, 32);

    ASTIA_CHECK(evaluate_rlimit(RLIMIT_AS, astia_rlimit{64, 64}, current, false).error ==
                -EPERM);
    ASTIA_CHECK(accepted(evaluate_rlimit(RLIMIT_AS, astia_rlimit{16, 32}, current, false),
                         16, 32));
}

ASTIA_TEST(rlimit_rejects_soft_limit_above_hard_limit) {
    const auto current = state(32, 32);

    for (const bool privileged : {false, true}) {
        ASTIA_CHECK(
            evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{64, 32}, current, privileged)
                .error == -EINVAL);
        ASTIA_CHECK(
            evaluate_rlimit(RLIMIT_AS, astia_rlimit{64, 32}, current, privileged).error ==
            -EINVAL);
    }
}

ASTIA_TEST(rlimit_rejects_nofile_hard_limit_above_kernel_maximum) {
    const auto current = state(32, 32);

    for (const bool privileged : {false, true}) {
        ASTIA_CHECK(
            evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{32, 1025}, current, privileged)
                .error == -EINVAL);
        ASTIA_CHECK(evaluate_rlimit(RLIMIT_NOFILE, astia_rlimit{infinity, infinity},
                                    current, privileged)
                        .error == -EINVAL);
    }
}

ASTIA_TEST(rlimit_rejects_address_space_below_mapped_bytes) {
    auto current = state(32, 32);

    current.address_space = astia_rlimit{64, 1024};
    current.mapped_bytes  = 64;

    const astia_rlimit request{32, 512};

    for (const bool privileged : {false, true}) {
        ASTIA_CHECK(evaluate_rlimit(RLIMIT_AS, request, current, privileged).error ==
                    -EINVAL);
    }
}

ASTIA_TEST(rlimit_rejects_unknown_resources) {
    const auto current = state(32, 32);

    for (const bool privileged : {false, true}) {
        ASTIA_CHECK(evaluate_rlimit(0, astia_rlimit{1, 1}, current, privileged).error ==
                    -EINVAL);
    }
}
