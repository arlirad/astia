/*
 * Host port of the futex calls pthread_once() needs.  The object under test
 * is the shipping libc/src/process/pthread_once.c, compiled directly into
 * this test; its futex_wait()/futex_wake() land here and go to the host
 * kernel, so the once protocol runs against a real futex and a real race.
 *
 * The host errno is glibc's while <errno.h> on this include path is ours, so
 * the translation is explicit -- same split as backend_host.c.
 */

#include <sys/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern long syscall(long number, ...);
extern int* __errno_location(void); /* glibc's errno, without its header */
#define HOST_ERRNO (*__errno_location())

enum {
    FUTEX_WAIT_PRIVATE = 128,
    FUTEX_WAKE_PRIVATE = 129,
};

int futex_wait(uint32_t* address, uint32_t expected) {
    long rc = syscall(SYS_futex, address, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
    if (rc < 0) {
        errno = HOST_ERRNO;

        return -1;
    }

    return (int) rc;
}

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
    if (rc < 0) {
        errno = HOST_ERRNO;

        return -1;
    }

    return (int) rc;
}
