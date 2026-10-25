#include "common.h"

#include <sys/stat.h>

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int force = 0;
    int status = 0;
    int operands = 0;
    const char* path;
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("rm", "[-f] FILE...", "Remove files.  Recursive removal is not supported.");
        if (cu_is_version(path))
            return cu_version("rm");
        if (args.options && cu_arg_is(path, "-f", "--force")) {
            force = 1;
            continue;
        }
        if (args.options && (cu_arg_is(path, "-r", "--recursive") ||
                             cu_arg_is(path, "-R", 0)))
            return cu_error("rm", "recursive removal is not supported");
        if (args.options && path[0] == '-')
            return cu_error("rm", "unknown option");
        ++operands;
        if (unlink(path) < 0 && !(force && errno == ENOENT))
            status |= cu_error("rm", path);
    }
    if (operands == 0)
        return cu_usage("rm", "[-f] FILE...");
    return status;
}
