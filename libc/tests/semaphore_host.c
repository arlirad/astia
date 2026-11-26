#include <sys/futex.h>
#include <sys/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <time.h>

extern long syscall(long number, ...);
extern int* __errno_location(void);

int futex_wait(uint32_t* address, uint32_t expected) {
    long rc = syscall(SYS_futex, address, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
    if (rc < 0) {
        errno = *__errno_location();

        return -1;
    }

    return (int) rc;
}

int futex_wait_until(uint32_t* address, uint32_t expected,
                     const struct timespec* deadline) {
    struct timespec now;
    struct timespec relative;

    long rc;

    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &now) != 0) {
        errno = *__errno_location();

        return -1;
    }

    relative.tv_sec  = deadline->tv_sec - now.tv_sec;
    relative.tv_nsec = deadline->tv_nsec - now.tv_nsec;

    if (relative.tv_nsec < 0) {
        relative.tv_sec--;

        relative.tv_nsec += 1000000000L;
    }

    if (relative.tv_sec < 0 || (relative.tv_sec == 0 && relative.tv_nsec <= 0)) {
        errno = ETIMEDOUT;

        return -1;
    }

    rc = syscall(SYS_futex, address, FUTEX_WAIT_PRIVATE, expected, &relative, NULL, 0);
    if (rc < 0) {
        errno = *__errno_location();

        return -1;
    }

    return (int) rc;
}

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
    if (rc < 0) {
        errno = *__errno_location();

        return -1;
    }

    return (int) rc;
}
