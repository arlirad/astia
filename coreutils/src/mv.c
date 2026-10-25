#include "common.h"

#include <fcntl.h>

static int move_file(const char* source, const char* destination) {
    if (rename(source, destination) == 0) return 0;
    if (errno != EXDEV) return cu_error("mv", source);

    /* Cross-filesystem moves cannot be atomic; retain the traditional
       copy-and-unlink fallback, but never hide native rename errors. */
    int input = open(source, O_RDONLY);
    if (input < 0) return cu_error("mv", source);
    int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        close(input);
        return cu_error("mv", destination);
    }
    char buffer[4096];
    int status = 0;
    for (;;) {
        long count = read(input, buffer, sizeof buffer);
        if (count == 0) break;
        if (count < 0 || cu_write_all(output, buffer, (size_t)count) < 0) {
            status = cu_error("mv", source);
            break;
        }
    }
    close(input);
    close(output);
    if (status != 0 || unlink(source) < 0)
        return status != 0 ? status : cu_error("mv", source);
    return 0;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    const char* source = 0;
    const char* destination = 0;
    const char* value;
    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("mv", "SOURCE DEST", "Rename one file or directory to DEST.");
        if (cu_is_version(value))
            return cu_version("mv");
        if (args.options && value[0] == '-')
            return cu_error("mv", "unknown option");
        if (source == 0)
            source = value;
        else if (destination == 0)
            destination = value;
        else
            return cu_usage("mv", "SOURCE DEST");
    }
    if (source == 0 || destination == 0)
        return cu_usage("mv", "SOURCE DEST");
    return move_file(source, destination);
}
