#include "common.h"

#include <fcntl.h>
#include <stdlib.h>

static int copy_fd(int fd, const char* name) {
    char buffer[4096];
    for (;;) {
        long count = read(fd, buffer, sizeof buffer);
        if (count == 0)
            return 0;
        if (count < 0)
            return cu_error("cat", name);
        if (cu_write_all(CU_STDOUT, buffer, (size_t) count) < 0)
            return 1;
    }
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int status = 0;
    if (argc == 1)
        return copy_fd(CU_STDIN, "standard input");
    const char* path;
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("cat", "[FILE]...",
                           "Copy FILEs to standard output; - means standard input.");
        if (cu_is_version(path))
            return cu_version("cat");
        if (args.options && path[0] == '-')
            return cu_error("cat", "unknown option");
        if (__builtin_strcmp(path, "-") == 0) {
            status |= copy_fd(CU_STDIN, "standard input");
            continue;
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            status |= cu_error("cat", path);
            continue;
        }
        status |= copy_fd(fd, path);
        if (close(fd) < 0)
            status |= cu_error("cat", path);
    }
    return status;
}
