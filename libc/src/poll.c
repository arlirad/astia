#include <poll.h>
#include <errno.h>
#include "file_port.h"

int poll(struct pollfd* fds, size_t count, int timeout_ms) {
    long rc = __astia_fio_poll(fds, count, timeout_ms);
    if (rc < 0) { errno = (int)-rc; return -1; }
    return (int)rc;
}
