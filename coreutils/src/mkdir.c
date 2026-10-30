#include "common.h"
#include <sys/stat.h>

#include <string.h>

static int make_parents(const char* path) {
    char   parent[256];
    size_t length = __builtin_strlen(path);
    if (length == 0 || length >= sizeof parent)
        return cu_error("mkdir", path);
    for (size_t i = 0; i < length; ++i)
        parent[i] = path[i];
    parent[length] = '\0';

    for (size_t i = 1; i < length; ++i) {
        if (parent[i] != '/')
            continue;
        parent[i] = '\0';
        if (parent[0] != '\0' && mkdir(parent, 0777) < 0 && errno != EEXIST) {
            parent[i] = '/';
            return cu_error("mkdir", strerror(errno));
        }
        parent[i] = '/';
    }
    if (mkdir(parent, 0777) < 0 && errno != EEXIST)
        return cu_error("mkdir", strerror(errno));
    if (errno == EEXIST) {
        struct stat info;
        if (stat(parent, &info) < 0 || !S_ISDIR(info.st_mode))
            return cu_error("mkdir", strerror(errno));
    }
    return 0;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int         parents  = 0;
    int         status   = 0;
    int         operands = 0;
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
