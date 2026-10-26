#include "common.h"

#include <fcntl.h>
#include <stdio.h>

static int count_fd(int fd, const char* name, int lines_only) {
    char          buffer[4096];
    unsigned long lines = 0, words = 0, bytes = 0;
    int           in_word = 0;
    for (;;) {
        long count = read(fd, buffer, sizeof buffer);
        if (count == 0)
            break;
        if (count < 0)
            return cu_error("wc", name);
        bytes += (unsigned long) count;
        for (long i = 0; i < count; ++i) {
            char c = buffer[i];
            if (c == '\n')
                ++lines;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r')
                in_word = 0;
            else if (!in_word) {
                in_word = 1;
                ++words;
            }
        }
    }
    char output[96];
    int  length = lines_only
                      ? snprintf(output, sizeof output, "%lu\n", lines)
                      : snprintf(output, sizeof output, "%lu %lu %lu %s\n", lines, words,
                                 bytes, name);
    return length < 0 || cu_write_all(CU_STDOUT, output, (size_t) length) < 0;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int lines_only = 0;
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("wc", "[FILE]...", "Print line, word, and byte counts for each FILE.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("wc");
    if (argc == 1)
        return count_fd(CU_STDIN, "", 0);
    int status = 0;
    int operands = 0;
    const char* path;
    while ((path = cu_args_next(&args)) != 0) {
        if (args.options && path[0] == '-') {
            if (cu_arg_is(path, "-l", 0)) {
                lines_only = 1;
                continue;
            }
            return cu_error("wc", "unknown option");
        }
        ++operands;
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            status |= cu_error("wc", path);
            continue;
        }
        status |= count_fd(fd, path, lines_only);
        close(fd);
    }
    if (operands == 0)
        return count_fd(CU_STDIN, "", lines_only);
    return status;
}
