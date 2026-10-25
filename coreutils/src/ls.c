#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

static int list_one(const char* path, int header) {
    DIR* directory = opendir(path);
    if (directory == 0)
        return cu_error("ls", strerror(errno));
    if (header && (cu_puts(path) < 0 || cu_puts(":\n") < 0)) {
        closedir(directory);
        return 1;
    }
    struct dirent* entry;
    int            status = 0;
    errno                 = 0;
    while ((entry = readdir(directory)) != 0) {
        if (cu_puts(entry->d_name) < 0 || cu_puts("\n") < 0) {
            status = 1;
            break;
        }
    }
    if (errno != 0)
        status = cu_error("ls", strerror(errno));
    closedir(directory);
    return status;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int status  = 0;
    int headers = 0;
    int operands = 0;
    const char* path;
    if (argc == 1)
        return list_one(".", 0);
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("ls", "[OPTION]... [FILE]...", "List directory contents.  -a is accepted for compatibility.");
        if (cu_is_version(path))
            return cu_version("ls");
        if (cu_arg_is(path, "-a", "--all"))
            continue;
        if (path[0] == '-')
            return cu_error("ls", "unknown option");
        headers = 1;
        ++operands;
        status |= list_one(path, headers);
    }
    if (operands == 0)
        return list_one(".", 0);
    return status;
}
