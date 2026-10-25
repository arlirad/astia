#include "common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

static int touch_one(const char* path) {
    struct stat info;
    int fd;
    if (stat(path, &info) < 0) {
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
    cu_args args;
    const char* path;
    int operands = 0;
    int status = 0;
    cu_args_init(&args, argc, argv);
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("touch", "FILE...", "Create FILE if missing, or update its modification time.");
        if (cu_is_version(path))
            return cu_version("touch");
        if (args.options && path[0] == '-')
            return cu_error("touch", "unknown option");
        ++operands;
        status |= touch_one(path);
    }
    return operands == 0 ? cu_usage("touch", "FILE...") : status;
}
