#include "internal/process_port.h"
#include <sys/futex.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>

/* A once control packs the owning process id into the high word and a phase
 * into the low word, which is also the futex word:
 *
 *   pthread_once_never     nobody has started the initializer
 *   pthread_once_running   the thread in the owning process runs it
 *   pthread_once_done      it completed; every later call returns at once
 *
 * Exactly one caller wins the compare-and-exchange that publishes
 * pthread_once_running and runs the initializer; everyone else waits for the
 * low word to stop being pthread_once_running.  The winner publishes
 * pthread_once_done with a release store, and a waiter's acquire reload of
 * the control is what makes the initializer's effects visible to it.
 *
 * A control still running under another process id is the fork case: the
 * child has no thread to wait for, so it takes the control over and runs the
 * initializer itself.  A finished control stays finished across fork because
 * the child inherits the initializer's effects.
 *
 * The initializer runs with cancellation disabled.  pthread_once is not a
 * cancellation point, and a control abandoned while running would strand
 * every later caller, so the winner always reaches the publication below and
 * restores the caller's cancellation state afterwards.  A cancellation
 * requested in the meantime is delivered by that restore, after the done
 * state is stored and the waiters are woken: the control is never left in
 * the running phase and a retry can never race with a partially applied
 * initializer.  POSIX leaves the cancellation behaviour of the initializer
 * unspecified; this trades the ability to cancel a running initializer for a
 * once control that cannot deadlock.
 */
enum {
    pthread_once_never   = 0,
    pthread_once_running = 1,
    pthread_once_done    = 2,
};

/* The futex ABI reads the wake count as a signed int: UINT32_MAX turns into
 * -1 and releases a single waiter, leaving the rest of them parked. */
enum {
    pthread_once_wake_all = INT_MAX,
};

static uint64_t pthread_once_state(int pid, int phase) {
    return ((uint64_t) (uint32_t) pid << 32) | (uint32_t) phase;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    const long pid = __astia_process_getpid();

    const uint64_t running = pthread_once_state((int) pid, pthread_once_running);

    if (once_control == NULL || init_routine == NULL || pid <= 0)
        return EINVAL;

    for (;;) {
        const uint64_t state = __atomic_load_n(once_control, __ATOMIC_ACQUIRE);
        const uint32_t phase = (uint32_t) state;

        if (phase == pthread_once_done)
            return 0;

        if (phase != pthread_once_never && phase != pthread_once_running)
            return EINVAL;

        if (phase == pthread_once_never || (uint32_t) (state >> 32) != (uint32_t) pid) {
            uint64_t expected = state;

            if (__atomic_compare_exchange_n(once_control, &expected, running, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                break;

            continue;
        }

        const int result = futex_wait((uint32_t*) once_control, pthread_once_running);

        if (result < 0 && errno != EAGAIN && errno != EINTR)
            return errno;
    }

    int cancel_state;

    (void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);

    init_routine();

    __atomic_store_n(once_control, pthread_once_state((int) pid, pthread_once_done),
                     __ATOMIC_RELEASE);
    (void) futex_wake((uint32_t*) once_control, pthread_once_wake_all);
    (void) pthread_setcancelstate(cancel_state, NULL);

    return 0;
}
