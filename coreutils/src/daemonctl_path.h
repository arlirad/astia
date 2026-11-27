#pragma once

#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stddef.h>

static int cu_daemonctl_socket_address(struct sockaddr_un* address, const char* path,
                                       size_t* path_length) {
    size_t length = __builtin_strlen(path);

    if (length >= sizeof address->sun_path) {
        errno = ENAMETOOLONG;

        return -1;
    }

    __builtin_memset(address, 0, sizeof *address);

    address->sun_family = AF_UNIX;

    __builtin_memcpy(address->sun_path, path, length + 1);

    if (path_length != NULL)
        *path_length = length;

    return 0;
}
