#include <astia/api/aio.h>

#include "file_port.h"
#include "time_port.h"

#include <time.h>

static int supported_clock(int clock_id) {
    return clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_REALTIME ||
           clock_id == CLOCK_BOOTTIME;
}

static int timespec_before(int64_t left_sec, int64_t left_nsec, int64_t right_sec,
                           int64_t right_nsec) {
    return left_sec < right_sec || (left_sec == right_sec && left_nsec < right_nsec);
}

static uint64_t monotonic_nanoseconds(int64_t sec, int64_t nsec) {
    uint64_t seconds = (uint64_t) sec;

    if (seconds > UINT64_MAX / 1000000000ull)
        return UINT64_MAX;

    seconds *= 1000000000ull;

    return UINT64_MAX - seconds < (uint64_t) nsec ? UINT64_MAX
                                                  : seconds + (uint64_t) nsec;
}

static uint64_t deadline_from_delta(uint64_t now, int64_t sec, int64_t nsec,
                                    int64_t now_sec, int64_t now_nsec) {
    uint64_t seconds;
    uint64_t delta;

    if (timespec_before(sec, nsec, now_sec, now_nsec))
        return now;

    seconds = (uint64_t) (sec - now_sec);

    if (nsec < now_nsec) {
        --seconds;

        nsec += 1000000000;
    }

    if (seconds > UINT64_MAX / 1000000000ull) {
        delta = UINT64_MAX;
    }
    else {
        delta = seconds * 1000000000ull;

        if (UINT64_MAX - delta < (uint64_t) (nsec - now_nsec))
            delta = UINT64_MAX;
        else
            delta += (uint64_t) (nsec - now_nsec);
    }

    return UINT64_MAX - now < delta ? UINT64_MAX : now + delta;
}

long __astia_time_nanosleep(int64_t sec, int64_t nsec, int64_t* remaining_sec,
                            int64_t* remaining_nsec) {
    uint64_t seconds = (uint64_t) sec;
    uint64_t nanoseconds;
    uint64_t remaining = 0;

    long rc;

    if (seconds > UINT64_MAX / 1000000000ull) {
        nanoseconds = UINT64_MAX;
    }
    else {
        nanoseconds = seconds * 1000000000ull;

        if (UINT64_MAX - nanoseconds < (uint64_t) nsec)
            nanoseconds = UINT64_MAX;
        else
            nanoseconds += (uint64_t) nsec;
    }

    rc = __astia_fio_sleep(nanoseconds, &remaining);

    if (rc == -EINTR) {
        if (remaining_sec != NULL)
            *remaining_sec = (int64_t) (remaining / 1000000000ull);

        if (remaining_nsec != NULL)
            *remaining_nsec = (int64_t) (remaining % 1000000000ull);
    }

    return rc;
}

long __astia_time_clock_nanosleep(int clock_id, int flags, int64_t sec, int64_t nsec,
                                  int64_t* remaining_sec, int64_t* remaining_nsec) {
    int64_t  now_sec;
    int64_t  now_nsec;
    int64_t  mono_sec;
    int64_t  mono_nsec;
    uint64_t deadline;

    long rc;

    if (!supported_clock(clock_id) || (flags & ~TIMER_ABSTIME) != 0)
        return -EINVAL;

    if ((flags & TIMER_ABSTIME) == 0)
        return __astia_time_nanosleep(sec, nsec, remaining_sec, remaining_nsec);

    rc = __astia_time_clock_gettime(clock_id, &now_sec, &now_nsec);
    if (rc < 0)
        return rc;

    if (!timespec_before(now_sec, now_nsec, sec, nsec))
        return 0;

    rc = __astia_time_clock_gettime(CLOCK_MONOTONIC, &mono_sec, &mono_nsec);
    if (rc < 0)
        return rc;

    deadline = deadline_from_delta(monotonic_nanoseconds(mono_sec, mono_nsec), sec, nsec,
                                   now_sec, now_nsec);

    return __astia_fio_sleep_absolute(deadline);
}
