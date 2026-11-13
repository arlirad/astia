#include "common.h"
#include <sys/stat.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int touch_one(const char* path, int no_create, int access_only) {
    struct stat info;
    int         fd;
    if (access_only)
        return cu_error("touch", "selective timestamp updates unsupported");
    int         exists = stat(path, &info) == 0;
    if (!exists && errno != ENOENT)
        return cu_error("touch", strerror(errno));
    if (!exists) {
        if (no_create)
            return 0;
        fd = open(path, O_WRONLY | O_CREAT, 0666);
        if (fd < 0)
            return cu_error("touch", strerror(errno));
        if (close(fd) < 0)
            return cu_error("touch", strerror(errno));
    }
    if (utime(path, NULL) < 0)
        return cu_error("touch", strerror(errno));
    return 0;
}

int main(int argc, char** argv) {
    cu_args     args;
    const char* path;
    int         operands    = 0;
    int         status      = 0;
    int         no_create   = 0;
    int         access_only = 0;
    cu_args_init(&args, argc, argv);
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help(
                "touch", "[-acm] FILE...",
                "Create FILE if missing, or update its native modification timestamp. -c skips creation.");
        if (cu_is_version(path))
            return cu_version("touch");
        if (args.options && cu_arg_is(path, "-c", "--no-create")) {
            no_create = 1;
            continue;
        }
        if (args.options && cu_arg_is(path, "-a", "--access")) {
            access_only = 1;
            continue;
        }
        if (args.options && cu_arg_is(path, "-m", "--modify")) {
            continue;
        }
        if (args.options && path[0] == '-')
            return cu_error("touch", "unknown option");
        ++operands;
        status |= touch_one(path, no_create, access_only);
    }
    return operands == 0 ? cu_usage("touch", "[-acm] FILE...") : status;
}
