#include <sys/futex.h>
#include <sys/syscall.h>

#include <errno.h>
#include <stddef.h>

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

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
    if (rc < 0) {
        errno = *__errno_location();
        return -1;
    }
    return (int) rc;
}
