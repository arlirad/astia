#include "../src/read_retry.h"

#include <stdio.h>
#include <string.h>

static int injected;

extern long __real_read(int fd, void* buffer, size_t length);

long __wrap_read(int fd, void* buffer, size_t length) {
    if (!injected) {
        injected = 1;
        errno    = EINTR;

        return -1;
    }

    return __real_read(fd, buffer, length);
}

int main(void) {
    int pipe_fds[2];

    if (pipe(pipe_fds) < 0)
        return 1;

    static const char   payload[]      = "payload";
    static const size_t payload_length = sizeof payload - 1;

    if (write(pipe_fds[1], payload, payload_length) != (long) payload_length)
        return 1;

    char buffer[sizeof payload] = {0};

    long count = cu_read_retry(pipe_fds[0], buffer, payload_length);

    int ok = injected && count == (long) payload_length &&
             memcmp(buffer, payload, payload_length) == 0;

    close(pipe_fds[0]);
    close(pipe_fds[1]);

    if (!ok) {
        printf("read retry test failed: injected=%d count=%ld\n", injected, count);

        return 1;
    }

    printf("read retry test ok\n");

    return 0;
}
