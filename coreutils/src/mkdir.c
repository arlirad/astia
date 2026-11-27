#include "common.h"
#include <sys/stat.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static unsigned int current_umask(void) {
    unsigned int value = umask(0);

    (void) umask(value);

    return value;
}

static int make_parents(const char* path) {
    size_t length = strlen(path);
    if (length == 0)
        return cu_error("mkdir", path);

    if (length >= PATH_MAX)
        return cu_error("mkdir", strerror(ENAMETOOLONG));

    char* parent = (char*) malloc(length + 1);
    if (parent == 0)
        return cu_error("mkdir", "out of memory");

    memcpy(parent, path, length + 1);

    int status = 0;

    for (size_t i = 1; i < length; ++i) {
        if (parent[i] != '/')
            continue;

        parent[i] = '\0';

        if (parent[0] != '\0') {
            if (mkdir(parent, 0777) < 0) {
                if (errno != EEXIST) {
                    parent[i] = '/';

                    status = cu_error("mkdir", strerror(errno));

                    goto done;
                }

                struct stat info;

                if (stat(parent, &info) < 0 || !S_ISDIR(info.st_mode)) {
                    parent[i] = '/';

                    status = cu_error("mkdir", parent);

                    goto done;
                }
            }
            else if (chmod(parent, 0300 | (0777 & ~current_umask())) < 0) {
                parent[i] = '/';

                status = cu_error("mkdir", strerror(errno));

                goto done;
            }
        }

        parent[i] = '/';
    }

    if (mkdir(parent, 0777) < 0) {
        if (errno != EEXIST) {
            status = cu_error("mkdir", strerror(errno));
        }
        else {
            struct stat info;

            if (stat(parent, &info) < 0 || !S_ISDIR(info.st_mode))
                status = cu_error("mkdir", strerror(errno));
        }
    }

done:
    free(parent);

    return status;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    int parents  = 0;
    int status   = 0;
    int operands = 0;

    const char* path;

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("mkdir", "[-p] DIRECTORY...",
                           "Create DIRECTORY; -p creates missing parent directories.");

        if (cu_is_version(path))
            return cu_version("mkdir");

        if (args.options && cu_arg_is(path, "-p", "--parents")) {
            parents = 1;

            continue;
        }

        if (args.options && path[0] == '-')
            return cu_error("mkdir", "unknown option");

        ++operands;

        if (parents)
            status |= make_parents(path);
        else if (mkdir(path, 0777) < 0)
            status |= cu_error("mkdir", strerror(errno));
    }

    return operands == 0 ? cu_usage("mkdir", "[-p] DIRECTORY...") : status;
}
