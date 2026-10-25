#include "common.h"

#include <fcntl.h>

static int copy_fd(int input, int output, const char* name) {
    char buffer[4096];
    for (;;) {
        long count = read(input, buffer, sizeof buffer);
        if (count == 0)
            return 0;
        if (count < 0 || cu_write_all(output, buffer, (size_t)count) < 0)
            return cu_error("cp", name);
    }
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    const char* source = 0;
    const char* destination = 0;
    const char* value;
    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("cp", "SOURCE DEST", "Copy one file to DEST; - denotes standard input or output.");
        if (cu_is_version(value))
            return cu_version("cp");
        if (args.options && value[0] == '-' && __builtin_strcmp(value, "-") != 0)
            return cu_error("cp", "unknown option");
        if (source == 0)
            source = value;
        else if (destination == 0)
            destination = value;
        else
            return cu_usage("cp", "SOURCE DEST");
    }
    if (source == 0 || destination == 0)
        return cu_usage("cp", "SOURCE DEST");

    int input = CU_STDIN;
    int output = CU_STDOUT;
    if (__builtin_strcmp(source, "-") != 0) {
        input = open(source, O_RDONLY);
        if (input < 0)
            return cu_error("cp", source);
    }
    if (__builtin_strcmp(destination, "-") != 0) {
        output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output < 0) {
            if (input != CU_STDIN) close(input);
            return cu_error("cp", destination);
        }
    }
    int status = copy_fd(input, output, source);
    if (input != CU_STDIN)
        close(input);
    if (output != CU_STDOUT)
        close(output);
    return status;
}
