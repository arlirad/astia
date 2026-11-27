#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include <sys/random.h>

#include <errno.h>
#include <stddef.h>

/* The raw trap behaves like a normal call: the kernel does not preserve
 * caller-saved argument registers.  Keep the call out of line so callers
 * such as getentropy() keep `length` somewhere a syscall cannot clobber. */
__attribute__((noinline)) static long __astia_getrandom_call(void* buffer, size_t length,
                                                             unsigned flags) {
    return astia_getrandom(buffer, (uint64_t) length, (uint32_t) flags);
}

ssize_t getrandom(void* buffer, size_t length, unsigned flags) {
    const long rc = __astia_getrandom_call(buffer, length, flags);
    if (rc < 0) {
        errno = (int) -rc;

        return (ssize_t) -1;
    }

    return (ssize_t) rc;
}

int getentropy(void* buffer, size_t length) {
    if (length > 256) {
        errno = EIO;

        return -1;
    }

    const ssize_t rc = getrandom(buffer, length, 0);
    if (rc < 0)
        return -1;

    if ((size_t) rc != length) {
        errno = EIO;

        return -1;
    }

    return 0;
}
