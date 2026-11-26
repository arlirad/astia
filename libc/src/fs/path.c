#include "internal/path.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

int __astia_resolve_path(const char* path, char* out, unsigned long capacity) {
    char          cwd[astia_path_buffer_capacity];
    unsigned long at = 0;
    unsigned long i;

    if (path == 0 || out == 0 || capacity == 0) {
        errno = EINVAL;

        return -1;
    }

    if (path[0] == '/') {
        for (i = 0; path[i] != '\0' && i + 1 < capacity; ++i) {
            out[i] = path[i];
        }

        if (path[i] != '\0') {
            errno = ENAMETOOLONG;

            return -1;
        }

        out[i] = '\0';

        return 0;
    }

    if (getcwd(cwd, sizeof cwd) == 0)
        return -1;

    if (strcmp(path, ".") == 0) {
        for (i = 0; cwd[i] != '\0' && i + 1 < capacity; ++i) {
            out[i] = cwd[i];
        }

        if (cwd[i] != '\0') {
            errno = ENAMETOOLONG;

            return -1;
        }

        out[i] = '\0';

        return 0;
    }

    for (i = 0; cwd[i] != '\0' && at + 1 < capacity; ++i) {
        out[at++] = cwd[i];
    }

    if (at > 1 && at + 1 < capacity)
        out[at++] = '/';

    for (i = 0; path[i] != '\0' && at + 1 < capacity; ++i) {
        out[at++] = path[i];
    }

    if (path[i] != '\0') {
        errno = ENAMETOOLONG;

        return -1;
    }

    out[at] = '\0';

    return 0;
}
