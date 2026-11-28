#include <sys/file.h>

#include <errno.h>

/* Advisory locking is a no-op until the kernel grows a lock provider. */
int flock(int fd, int operation) {
    if (fd < 0) {
        errno = EBADF;

        return -1;
    }

    (void) operation;

    return 0;
}
