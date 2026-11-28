/*
 * pthread_once: several threads race on one control while the winner's
 * initializer is held in place, so the rest must wait and be woken when it
 * finishes.  The initializer has to run exactly once per control, and a
 * finished control must stay terminal.
 *
 * The object under test is the shipping src/process/pthread_once.c, compiled
 * into this binary (hyphenated: host pthreads are the thread source only).
 * support/futex_host.c puts a real host futex under it, and errno is the
 * shipping thread-local object, so a lost or single wake shows up as a hang
 * rather than as a silently passing test.
 *
 * A second scenario cancels the thread running the initializer.  The shipping
 * implementation runs the initializer with cancellation disabled, so the
 * initializer completes, the control becomes done, and only then is the
 * pending cancellation delivered to the winner.  The waiter must therefore
 * observe completion instead of blocking on a stranded control, and the
 * control must end done so a later call is a no-op.
 */
#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>

extern int pthread_once(uint64_t* once_control, void (*init_routine)(void));

static int fails;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

enum {
    once_threads       = 4,
    once_rounds        = 50,
    once_cancel_rounds = 16,
};

/* Mirrors the phase values in src/process/pthread_once.c. */
enum {
    once_phase_running = 1,
    once_phase_done    = 2,
};

static uint64_t          once_control;
static volatile unsigned once_calls;
static volatile int      once_started;
static volatile int      once_release;

static uint64_t          once_cancel_control;
static volatile unsigned once_cancel_calls;
static volatile int      once_cancel_started;
static volatile int      once_cancel_release;
static volatile int      once_cancel_completed;
static volatile int      once_cancel_returned;

static void once_initializer(void) {
    __atomic_add_fetch(&once_calls, 1, __ATOMIC_SEQ_CST);

    once_started = 1;

    while (!once_release) {
        sched_yield();
    }
}

static void once_cancel_initializer(void) {
    __atomic_add_fetch(&once_cancel_calls, 1, __ATOMIC_SEQ_CST);

    once_cancel_started = 1;

    while (!once_cancel_release) {
        sched_yield();
    }

    pthread_testcancel();

    once_cancel_completed = 1;
}

static void* once_worker(void* opaque) {
    (void) opaque;

    return (void*) (unsigned long) (pthread_once(&once_control, once_initializer) == 0
                                        ? 0x678a
                                        : 0x678b);
}

static void* once_cancel_worker(void* opaque) {
    (void) opaque;

    const int result = pthread_once(&once_cancel_control, once_cancel_initializer);

    /* The cancellation requested while the initializer ran is still pending
     * here: it is delivered inside pthread_once on the shipping libc and at
     * this point on the host.  Either way the worker dies before returning. */
    pthread_testcancel();

    once_cancel_returned = 1;

    return (void*) (unsigned long) (result == 0 ? 0x678a : 0x678b);
}

static void once_round(void) {
    pthread_t workers[once_threads];
    void*     result;

    once_control = 0;
    once_calls   = 0;
    once_started = 0;
    once_release = 0;

    CHECK(pthread_create(&workers[0], NULL, once_worker, NULL) == 0);

    while (!once_started) {
        sched_yield();
    }

    for (unsigned int i = 1; i < once_threads; ++i) {
        CHECK(pthread_create(&workers[i], NULL, once_worker, NULL) == 0);
    }

    for (int i = 0; i < 8; ++i) {
        sched_yield();
    }

    once_release = 1;

    for (unsigned int i = 0; i < once_threads; ++i) {
        result = NULL;

        CHECK(pthread_join(workers[i], &result) == 0);
        CHECK(result == (void*) (unsigned long) 0x678a);
    }

    CHECK(once_calls == 1);

    CHECK(pthread_once(&once_control, once_initializer) == 0);
    CHECK(once_calls == 1);
}

static void once_cancel_round(void) {
    pthread_t workers[2];
    void*     result;

    once_cancel_control   = 0;
    once_cancel_calls     = 0;
    once_cancel_started   = 0;
    once_cancel_release   = 0;
    once_cancel_completed = 0;
    once_cancel_returned  = 0;

    CHECK(pthread_create(&workers[0], NULL, once_cancel_worker, NULL) == 0);

    while (!once_cancel_started) {
        sched_yield();
    }

    CHECK(pthread_create(&workers[1], NULL, once_cancel_worker, NULL) == 0);

    for (int i = 0; i < 8; ++i) {
        sched_yield();
    }

    CHECK(pthread_cancel(workers[0]) == 0);

    once_cancel_release = 1;

    result = NULL;

    CHECK(pthread_join(workers[0], &result) == 0);
    CHECK(result == PTHREAD_CANCELED);

    result = NULL;

    CHECK(pthread_join(workers[1], &result) == 0);
    CHECK(result == (void*) (unsigned long) 0x678a);

    CHECK(once_cancel_calls == 1);
    CHECK(once_cancel_completed == 1);
    CHECK(once_cancel_returned == 1);
    CHECK((uint32_t) once_cancel_control == once_phase_done);
    CHECK((uint32_t) once_cancel_control != once_phase_running);

    CHECK(pthread_once(&once_cancel_control, once_cancel_initializer) == 0);
    CHECK(once_cancel_calls == 1);
}

int main(void) {
    CHECK(pthread_once(NULL, once_initializer) == EINVAL);
    CHECK(pthread_once(&once_control, NULL) == EINVAL);

    for (int round = 0; round < once_rounds; ++round) {
        once_round();
    }

    for (int round = 0; round < once_cancel_rounds; ++round) {
        once_cancel_round();
    }

    if (fails != 0) {
        printf("pthread_once: %d failure(s)\n", fails);

        return 1;
    }

    printf("pthread_once: %d rounds ok\n", once_rounds);

    return 0;
}
