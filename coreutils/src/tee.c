#include "common.h"

#include <fcntl.h>

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    int append = 0;

    const char* value;

    int first_file = argc;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("tee", "[-a] [FILE]...",
                           "Copy standard input to standard output and FILEs.");

        if (cu_is_version(value))
            return cu_version("tee");

        if (args.options && __builtin_strcmp(value, "-a") == 0) {
            append = 1;

            continue;
        }

        if (args.options && value[0] == '-')
            return cu_error("tee", "unknown option");

        first_file = args.index - 1;

        break;
    }

    if (argc - first_file > 32)
        return cu_error("tee", "too many output files");

    int fds[32];
    int count  = 0;
    int status = 0;

    for (int i = first_file; i < argc; ++i) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0666);
        if (fd < 0) {
            cu_error("tee", argv[i]);

            status = 1;

            continue;
        }

        fds[count++] = fd;
    }

    char buffer[4096];

    for (;;) {
        long length = read(CU_STDIN, buffer, sizeof buffer);
        if (length == 0)
            break;

        if (length < 0) {
            status = 1;

            break;
        }

        if (cu_write_all(CU_STDOUT, buffer, (size_t) length) < 0)
            status = 1;

        for (int i = 0; i < count; ++i) {
            if (cu_write_all(fds[i], buffer, (size_t) length) < 0)
                status = 1;
        }
    }

    for (int i = 0; i < count; ++i) {
        close(fds[i]);
    }

    return status;
}
