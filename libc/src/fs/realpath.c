#include "internal/path.h"
#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { realpath_symlink_limit = 40 };

static void set_root(char* path, unsigned long* length) {
    path[0] = '/';
    path[1] = '\0';
    *length = 1;
}

static int append_component(char* path, unsigned long* length, const char* component,
                            unsigned long component_length, unsigned long capacity) {
    if (*length == 1) {
        if (1 + component_length >= capacity) {
            errno = ENAMETOOLONG;

            return -1;
        }

        memcpy(path + 1, component, component_length);

        *length = 1 + component_length;
    }
    else {
        if (*length + 1 + component_length >= capacity) {
            errno = ENAMETOOLONG;

            return -1;
        }

        path[*length] = '/';

        memcpy(path + *length + 1, component, component_length);

        *length += 1 + component_length;
    }

    path[*length] = '\0';

    return 0;
}

static void pop_component(char* path, unsigned long* length) {
    if (*length <= 1) {
        set_root(path, length);

        return;
    }

    while (*length > 1 && path[*length - 1] != '/') {
        --*length;
    }

    if (*length > 1)
        --*length;

    path[*length] = '\0';

    if (*length == 0)
        set_root(path, length);
}

static int replace_remaining(char* remaining, const char* link, unsigned long link_length,
                             const char* rest, unsigned long capacity) {
    unsigned long rest_length = strlen(rest);

    if (link_length + rest_length >= capacity) {
        errno = ENAMETOOLONG;

        return -1;
    }

    memmove(remaining + link_length, rest, rest_length + 1);
    memcpy(remaining, link, link_length);

    return 0;
}

char* realpath(const char* path, char* resolved_path) {
    char          resolved[astia_path_buffer_capacity];
    char          remaining[astia_path_buffer_capacity];
    unsigned long length;
    unsigned long symlinks = 0;

    if (path == NULL) {
        errno = EINVAL;

        return NULL;
    }

    if (path[0] == '\0') {
        errno = ENOENT;

        return NULL;
    }

    if (strlen(path) >= sizeof remaining) {
        errno = ENAMETOOLONG;

        return NULL;
    }

    memcpy(remaining, path, strlen(path) + 1);

    if (path[0] == '/') {
        set_root(resolved, &length);
    }
    else {
        if (getcwd(resolved, sizeof resolved) == NULL)
            return NULL;

        length = strlen(resolved);

        if (length >= sizeof resolved) {
            errno = ENAMETOOLONG;

            return NULL;
        }
    }

    int restarted;

    do {
        unsigned long position = 0;

        restarted = 0;

        for (;;) {
            while (remaining[position] == '/') {
                ++position;
            }

            if (remaining[position] == '\0')
                break;

            unsigned long start = position;

            while (remaining[position] != '\0' && remaining[position] != '/') {
                ++position;
            }

            unsigned long component_length = position - start;

            const char* component = remaining + start;

            if (component_length == 1 && component[0] == '.')
                continue;

            if (component_length == 2 && component[0] == '.' && component[1] == '.') {
                pop_component(resolved, &length);
                continue;
            }

            if (append_component(resolved, &length, component, component_length,
                                 sizeof resolved) != 0)
                return NULL;

            struct stat status;

            if (lstat(resolved, &status) != 0)
                return NULL;

            if (!S_ISLNK(status.st_mode)) {
                if (remaining[position] == '/' && !S_ISDIR(status.st_mode)) {
                    errno = ENOTDIR;

                    return NULL;
                }

                continue;
            }

            char link[astia_path_buffer_capacity];

            link[0] = '\0';

            long link_length = readlink(resolved, link, sizeof link);
            if (link_length < 0)
                return NULL;

            if (link_length == 0) {
                errno = ENOENT;

                return NULL;
            }

            if ((unsigned long) link_length >= sizeof link) {
                errno = ENAMETOOLONG;

                return NULL;
            }

            pop_component(resolved, &length);

            if (link[0] == '/')
                set_root(resolved, &length);

            if (replace_remaining(remaining, link, (unsigned long) link_length,
                                  remaining + position, sizeof remaining) != 0)
                return NULL;

            restarted = 1;

            break;
        }

        if (restarted && ++symlinks > realpath_symlink_limit) {
            errno = ELOOP;

            return NULL;
        }
    } while (restarted);

    if (resolved_path == NULL) {
        resolved_path = malloc(astia_path_buffer_capacity);
        if (resolved_path == NULL) {
            errno = ENOMEM;

            return NULL;
        }
    }

    memcpy(resolved_path, resolved, length + 1);

    return resolved_path;
}
