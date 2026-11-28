#include "common.h"
#include <sys/stat.h>

#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

static int update_times(const char* path, int atime, int mtime) {
    struct stat     info;
    struct timespec now;
    struct timespec times[2] = {
        {0, atime ? UTIME_NOW : UTIME_OMIT},
        {0, mtime ? UTIME_NOW : UTIME_OMIT},
    };

    if (atime && mtime)
        return utime(path, NULL);

    if (utimensat(AT_FDCWD, path, times, 0) == 0)
        return 0;

    if (errno != EINVAL)
        return -1;

    if (stat(path, &info) < 0 || clock_gettime(CLOCK_REALTIME, &now) < 0)
        return -1;

    times[0] = atime ? now : (struct timespec){info.st_atime, 0};
    times[1] = mtime ? now : (struct timespec){info.st_mtime, 0};

    return utimensat(AT_FDCWD, path, times, 0);
}

static int touch_one(const char* path, int no_create, int atime, int mtime) {
    struct stat info;

    int fd;

    int exists = stat(path, &info) == 0;

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

    if (update_times(path, atime, mtime) < 0)
        return cu_error("touch", strerror(errno));

    return 0;
}

int main(int argc, char** argv) {
    cu_args     args;
    const char* path;

    int operands  = 0;
    int status    = 0;
    int no_create = 0;
    int access    = 0;
    int modify    = 0;

    cu_args_init(&args, argc, argv);

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help(
                "touch", "[-acm] FILE...",
                "Create FILE if missing, or update its access and modification "
                "timestamps. -c skips creation.");

        if (cu_is_version(path))
            return cu_version("touch");

        if (args.options && cu_arg_is(path, "-c", "--no-create")) {
            no_create = 1;

            continue;
        }

        if (args.options && cu_arg_is(path, "-a", "--access")) {
            access = 1;

            continue;
        }

        if (args.options && cu_arg_is(path, "-m", "--modify")) {
            modify = 1;

            continue;
        }

        if (args.options && path[0] == '-')
            return cu_error("touch", "unknown option");

        ++operands;

        status |= touch_one(path, no_create, access || !modify, modify || !access);
    }

    return operands == 0 ? cu_usage("touch", "[-acm] FILE...") : status;
}
