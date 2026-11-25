/*
 * clock_getres must accept every clock ID that clock_gettime accepts.
 *
 * POSIX ties the two calls together: a clock that can be read must also
 * report its resolution.  clock_gettime handles CLOCK_MONOTONIC,
 * CLOCK_BOOTTIME and CLOCK_REALTIME; clock_getres used to reject
 * CLOCK_BOOTTIME with -EINVAL, so a program that enumerated all three clocks
 * (and took a failure to mean "broken clock") misbehaved.
 *
 * Pinned here:
 *   - each of the three IDs succeeds in *both* handlers;
 *   - CLOCK_BOOTTIME reports the same resolution as CLOCK_MONOTONIC (they
 *     share one tick source), and every resolution is one tick, rounded up;
 *   - a NULL resolution pointer succeeds for every supported ID;
 *   - an ID outside the set fails -EINVAL in both handlers, leaving the
 *     output untouched;
 *   - a timer that is not running yet fails -EIO, leaving the output
 *     untouched.
 *
 * Real: both syscall handlers (clock_getres.cpp, clock_gettime.cpp, compiled
 * into this file), user_ptr validation and copy_out from user_mem.hpp.
 * Stubbed: the timer (arch::timer_hz/timer_ticks) and the current process,
 * which is NULL — the "kernel client" path, so a host pointer is a valid
 * output address and no page map is involved.  The page-map edges that path
 * never reaches (address_space, kernel_frames, process::cow_fault) are
 * stubbed to count a probe, and the tests require zero probes.
 * Not involved: the syscall entry trap, the ABI decoder, libc.
 */

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/mm/address_space.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "support/test.hpp"

// support/test.hpp pulls in the host <stdio.h>, whose renameat2 flags collide
// with the ABI enum in <astia/api/aio.h> (same workaround as
// syscall_handler_prototype_test.cpp).
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include "../src/sys/syscalls/clock_getres.cpp"
#include "../src/sys/syscalls/clock_gettime.cpp"

namespace {
    astia::u32 timer_frequency      = 0;
    astia::u64 timer_count          = 0;
    int        address_space_probes = 0;

    constexpr astia::i64 garbage = 9;

    constexpr astia::i32 supported_clocks[] = {CLOCK_MONOTONIC, CLOCK_BOOTTIME,
                                               CLOCK_REALTIME};

    /// IDs on both sides of the supported set, including negatives that would
    /// pass an unsigned "< count" range check by accident.
    constexpr astia::i32 unsupported_clocks[] = {
        -1, 3, 4, 99, 0x7fffffff, static_cast<astia::i32>(0x80000000u)};

    void set_timer(astia::u32 hz, astia::u64 ticks) {
        timer_frequency      = hz;
        timer_count          = ticks;
        address_space_probes = 0;
    }

    astia::sys::user_ptr<astia_timespec> out_ptr(astia_timespec* ts) {
        return astia::sys::user_ptr<astia_timespec>(
            static_cast<astia::u64>(reinterpret_cast<astia::usize>(ts)));
    }

    astia::i64 getres(astia::i32 clock_id, astia_timespec* ts) {
        return astia::sys::syscalls::clock_getres::invoke(clock_id, out_ptr(ts));
    }

    astia::i64 gettime(astia::i32 clock_id, astia_timespec* ts) {
        return astia::sys::syscalls::clock_gettime::invoke(clock_id, out_ptr(ts));
    }

    bool untouched(const astia_timespec& ts) {
        return ts.tv_sec == garbage && ts.tv_nsec == garbage;
    }
} // namespace

namespace astia::arch {
    u32 timer_hz() {
        return timer_frequency;
    }

    u64 timer_ticks() {
        return timer_count;
    }
} // namespace astia::arch

namespace astia::proc {
    process* current_process() {
        return nullptr;
    }
} // namespace astia::proc

namespace astia::mm {
    // Reached only for a caller with an attached page map; current_process()
    // is NULL here, so each probe is a test failure, never a real answer.
    bool address_space::attached() const {
        ++address_space_probes;
        return false;
    }
    bool address_space::valid_user_range(u64, usize) const {
        ++address_space_probes;
        return false;
    }
    bool address_space::validate(u64, usize, access) const {
        ++address_space_probes;
        return false;
    }
    bool address_space::writable(vaddr_t) const {
        ++address_space_probes;
        return false;
    }
    paddr_t address_space::translate(vaddr_t) const {
        ++address_space_probes;
        return 0;
    }
    bool address_space::user_accessible(vaddr_t) const {
        ++address_space_probes;
        return false;
    }
    frame_allocator& kernel_frames() {
        ++address_space_probes;
        static frame_allocator instance;
        return instance;
    }
} // namespace astia::mm

namespace astia::proc {
    bool process::cow_fault(u64, bool) {
        ++address_space_probes;
        return false;
    }
} // namespace astia::proc

ASTIA_TEST(clock_getres_accepts_every_clock_id_clock_gettime_accepts) {
    set_timer(100, 1234);

    for (const astia::i32 clock_id : supported_clocks) {
        astia_timespec now = {garbage, garbage};
        astia_timespec res = {garbage, garbage};

        ASTIA_CHECK(gettime(clock_id, &now) == 0);
        ASTIA_CHECK(!untouched(now));
        ASTIA_CHECK(getres(clock_id, &res) == 0);
        ASTIA_CHECK(res.tv_sec == 0);
        ASTIA_CHECK(res.tv_nsec == 10000000);
    }
    ASTIA_CHECK(address_space_probes == 0);
}

ASTIA_TEST(clock_getres_boottime_matches_monotonic_resolution) {
    // 1000 Hz divides 1e9 exactly; 3 Hz does not and must round up, so a
    // reported tick is never finer than the clock can actually resolve.
    const astia::u32 rates[]  = {1000, 3, 1};
    const astia::i64 expect[] = {1000000, 333333334, 1000000000};

    for (astia::usize i = 0; i < sizeof rates / sizeof rates[0]; ++i) {
        set_timer(rates[i], 0);
        astia_timespec monotonic = {garbage, garbage};
        astia_timespec boottime  = {garbage, garbage};
        astia_timespec realtime  = {garbage, garbage};

        ASTIA_CHECK(getres(CLOCK_MONOTONIC, &monotonic) == 0);
        ASTIA_CHECK(getres(CLOCK_BOOTTIME, &boottime) == 0);
        ASTIA_CHECK(getres(CLOCK_REALTIME, &realtime) == 0);
        ASTIA_CHECK(monotonic.tv_sec == 0 && monotonic.tv_nsec == expect[i]);
        ASTIA_CHECK(boottime.tv_sec == monotonic.tv_sec);
        ASTIA_CHECK(boottime.tv_nsec == monotonic.tv_nsec);
        ASTIA_CHECK(realtime.tv_sec == monotonic.tv_sec);
        ASTIA_CHECK(realtime.tv_nsec == monotonic.tv_nsec);
    }
}

ASTIA_TEST(clock_gettime_boottime_reads_the_monotonic_tick_source) {
    // Pins the gettime side of the pair: BOOTTIME is the clock getres must
    // now describe, and it is the monotonic count (no suspend exists yet).
    set_timer(100, 1234);
    astia_timespec monotonic = {garbage, garbage};
    astia_timespec boottime  = {garbage, garbage};

    ASTIA_CHECK(gettime(CLOCK_MONOTONIC, &monotonic) == 0);
    ASTIA_CHECK(gettime(CLOCK_BOOTTIME, &boottime) == 0);
    ASTIA_CHECK(monotonic.tv_sec == 12 && monotonic.tv_nsec == 340000000);
    ASTIA_CHECK(boottime.tv_sec == monotonic.tv_sec);
    ASTIA_CHECK(boottime.tv_nsec == monotonic.tv_nsec);
}

ASTIA_TEST(clock_getres_null_output_succeeds_for_every_supported_clock) {
    set_timer(100, 0);

    for (const astia::i32 clock_id : supported_clocks)
        ASTIA_CHECK(getres(clock_id, nullptr) == 0);
    ASTIA_CHECK(address_space_probes == 0);
}

ASTIA_TEST(clock_getres_and_clock_gettime_reject_the_same_unknown_ids) {
    set_timer(100, 1234);

    for (const astia::i32 clock_id : unsupported_clocks) {
        astia_timespec now = {garbage, garbage};
        astia_timespec res = {garbage, garbage};

        ASTIA_CHECK(gettime(clock_id, &now) == -EINVAL);
        ASTIA_CHECK(untouched(now));
        ASTIA_CHECK(getres(clock_id, &res) == -EINVAL);
        ASTIA_CHECK(untouched(res));
        // NULL output does not turn an unknown ID into success.
        ASTIA_CHECK(getres(clock_id, nullptr) == -EINVAL);
    }
}

ASTIA_TEST(clock_getres_reports_eio_before_the_timer_runs) {
    set_timer(0, 0);

    for (const astia::i32 clock_id : supported_clocks) {
        astia_timespec now = {garbage, garbage};
        astia_timespec res = {garbage, garbage};

        ASTIA_CHECK(gettime(clock_id, &now) == -EIO);
        ASTIA_CHECK(untouched(now));
        ASTIA_CHECK(getres(clock_id, &res) == -EIO);
        ASTIA_CHECK(untouched(res));
    }
}

ASTIA_TEST(clock_getres_state_after_error_is_clean) {
    // A rejected ID must not poison the next valid call.
    set_timer(100, 0);
    astia_timespec res = {garbage, garbage};

    ASTIA_CHECK(getres(3, &res) == -EINVAL);
    ASTIA_CHECK(getres(CLOCK_BOOTTIME, &res) == 0);
    ASTIA_CHECK(res.tv_sec == 0 && res.tv_nsec == 10000000);
    ASTIA_CHECK(getres(CLOCK_BOOTTIME, &res) == 0);
    ASTIA_CHECK(res.tv_sec == 0 && res.tv_nsec == 10000000);
}
