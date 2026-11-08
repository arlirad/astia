#include <sys/futex.h>

#include <errno.h>
#include <semaphore.h>
#include <stddef.h>

enum { semaphore_initialized = 0x53454d31u };

static int semaphore_is_initialized(const sem_t* sem) {
    return sem != NULL &&
           __atomic_load_n(&sem->initialized, __ATOMIC_ACQUIRE) == semaphore_initialized;
}

static int semaphore_take(sem_t* sem, int nonblocking) {
    for (;;) {
        uint32_t value = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
        if (value != 0) {
            if (__atomic_compare_exchange_n(&sem->value, &value, value - 1, 0,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return 0;
            continue;
        }
        if (nonblocking) {
            errno = EAGAIN;
            return -1;
        }
        if (futex_wait(&sem->value, 0) < 0 && errno != EAGAIN)
            return -1;
    }
}

static int semaphore_take_until(sem_t* sem, const struct timespec* abstime) {
    struct timespec realtime;
    struct timespec monotonic;
    struct timespec deadline;
    int64_t         seconds;
    int64_t         nanoseconds;

    for (;;) {
        uint32_t value = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
        if (value != 0) {
            if (__atomic_compare_exchange_n(&sem->value, &value, value - 1, 0,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return 0;
            continue;
        }
        if (abstime == NULL || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) {
            errno = EINVAL;
            return -1;
        }
        if (clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
            clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
            return -1;
        seconds     = (int64_t) abstime->tv_sec - (int64_t) realtime.tv_sec;
        nanoseconds = (int64_t) abstime->tv_nsec - (int64_t) realtime.tv_nsec;
        if (nanoseconds < 0) {
            seconds--;
            nanoseconds += 1000000000L;
        }
        if (seconds < 0 || (seconds == 0 && nanoseconds == 0)) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (monotonic.tv_sec > INT64_MAX - seconds) {
            errno = EINVAL;
            return -1;
        }
        deadline.tv_sec  = monotonic.tv_sec + seconds;
        deadline.tv_nsec = monotonic.tv_nsec + nanoseconds;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        if (futex_wait_until(&sem->value, 0, &deadline) < 0) {
            if (errno != EAGAIN)
                return -1;
        }
    }
}

int sem_init(sem_t* sem, int pshared, unsigned int value) {
    if (sem == NULL || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (pshared != 0) {
        errno = ENOTSUP;
        return -1;
    }
    __atomic_store_n(&sem->value, value, __ATOMIC_RELAXED);
    __atomic_store_n(&sem->initialized, semaphore_initialized, __ATOMIC_RELEASE);
    return 0;
}

int sem_destroy(sem_t* sem) {
    if (!semaphore_is_initialized(sem)) {
        errno = EINVAL;
        return -1;
    }
    __atomic_store_n(&sem->initialized, 0, __ATOMIC_RELEASE);
    return 0;
}

int sem_wait(sem_t* sem) {
    if (!semaphore_is_initialized(sem)) {
        errno = EINVAL;
        return -1;
    }
    return semaphore_take(sem, 0);
}

int sem_timedwait(sem_t* sem, const struct timespec* abstime) {
    if (!semaphore_is_initialized(sem)) {
        errno = EINVAL;
        return -1;
    }
    return semaphore_take_until(sem, abstime);
}

int sem_trywait(sem_t* sem) {
    if (!semaphore_is_initialized(sem)) {
        errno = EINVAL;
        return -1;
    }
    return semaphore_take(sem, 1);
}

int sem_post(sem_t* sem) {
    if (!semaphore_is_initialized(sem)) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        uint32_t value = __atomic_load_n(&sem->value, __ATOMIC_RELAXED);
        if (value == SEM_VALUE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        if (__atomic_compare_exchange_n(&sem->value, &value, value + 1, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            (void) futex_wake(&sem->value, 1);
            return 0;
        }
    }
}

int sem_getvalue(const sem_t* sem, int* value) {
    if (!semaphore_is_initialized(sem) || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    *value = (int) __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
    return 0;
}
