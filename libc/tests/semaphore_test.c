#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif

#include <errno.h>
#include <semaphore.h>
#include <stdio.h>

static int fails;

#define CHECK(condition)                                                   \
    do {                                                                    \
        if (!(condition)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++fails;                                                        \
        }                                                                   \
    } while (0)

static sem_t        blocked_sem;
static volatile int worker_started;
static volatile int worker_finished;

static void* worker(void* argument) {
    (void) argument;
    worker_started = 1;
    CHECK(sem_wait(&blocked_sem) == 0);
    worker_finished = 1;
    return NULL;
}

int main(void) {
    sem_t sem = {0};
    int   value = -1;
    pthread_t thread;

    errno = 0;
    CHECK(sem_init(NULL, 0, 0) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(sem_init(&sem, 1, 0) == -1 && errno == ENOTSUP);
    errno = 0;
    CHECK(sem_init(&sem, 0, SEM_VALUE_MAX + 1U) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(sem_getvalue(&sem, &value) == -1 && errno == EINVAL);

    CHECK(sem_init(&sem, 0, 1) == 0);
    CHECK(sem_getvalue(&sem, &value) == 0 && value == 1);
    CHECK(sem_trywait(&sem) == 0);
    CHECK(sem_getvalue(&sem, &value) == 0 && value == 0);
    errno = 0;
    CHECK(sem_trywait(&sem) == -1 && errno == EAGAIN);
    CHECK(sem_post(&sem) == 0);
    CHECK(sem_getvalue(&sem, &value) == 0 && value == 1);
    CHECK(sem_wait(&sem) == 0);
    errno = 0;
    CHECK(sem_post(&sem) == 0);
    CHECK(sem_destroy(&sem) == 0);
    errno = 0;
    CHECK(sem_destroy(&sem) == -1 && errno == EINVAL);

    CHECK(sem_init(&blocked_sem, 0, 0) == 0);
    worker_started = 0;
    worker_finished = 0;
    CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
    while (!worker_started)
        ;
    for (volatile unsigned int spin = 0; spin < 1000000U && !worker_finished; ++spin)
        ;
    CHECK(!worker_finished);
    CHECK(sem_post(&blocked_sem) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(worker_finished);
    CHECK(sem_destroy(&blocked_sem) == 0);

    errno = 0;
    CHECK(sem_post(&sem) == -1 && errno == EINVAL);
    return fails != 0;
}
