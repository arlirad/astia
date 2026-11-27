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
    once_threads = 4,
    once_rounds  = 50,
};

static uint64_t          once_control;
static volatile unsigned once_calls;
static volatile int      once_started;
static volatile int      once_release;

static void once_initializer(void) {
    __atomic_add_fetch(&once_calls, 1, __ATOMIC_SEQ_CST);

    once_started = 1;

    while (!once_release) {
        sched_yield();
    }
}

static void* once_worker(void* opaque) {
    (void) opaque;

    return (void*) (unsigned long) (pthread_once(&once_control, once_initializer) == 0
                                        ? 0x678a
                                        : 0x678b);
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

int main(void) {
    CHECK(pthread_once(NULL, once_initializer) == EINVAL);
    CHECK(pthread_once(&once_control, NULL) == EINVAL);

    for (int round = 0; round < once_rounds; ++round) {
        once_round();
    }

    if (fails != 0) {
        printf("pthread_once: %d failure(s)\n", fails);

        return 1;
    }

    printf("pthread_once: %d rounds ok\n", once_rounds);

    return 0;
}
