#pragma once

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

static long cu_read_retry(int fd, void* buffer, size_t length) {
    for (;;) {
        long count = read(fd, buffer, length);

        if (count < 0 && errno == EINTR)
            continue;

        return count;
    }
}
