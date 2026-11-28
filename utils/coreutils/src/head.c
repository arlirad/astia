#include "common.h"
#include "read_retry.h"

#include <fcntl.h>
#include <stdlib.h>

#define HEAD_DEFAULT_LINES 10

enum {
    HEAD_OPTIONS_OK = 0,
    HEAD_OPTIONS_HELP,
    HEAD_OPTIONS_VERSION,
    HEAD_OPTIONS_INVALID
};

static int copy_lines(int fd, const char* name, unsigned long wanted) {
    char          buffer[4096];
    unsigned long lines = 0;

    if (wanted == 0)
        return 0;

    for (;;) {
        long count = cu_read_retry(fd, buffer, sizeof buffer);
        if (count == 0)
            return 0;

        if (count < 0)
            return cu_error("head", name);

        long used = 0;

        while (used < count) {
            if (buffer[used++] != '\n')
                continue;

            if (++lines == wanted) {
                if (cu_write_all(CU_STDOUT, buffer, (size_t) used) < 0)
                    return 1;

                return 0;
            }
        }

        if (cu_write_all(CU_STDOUT, buffer, (size_t) count) < 0)
            return 1;
    }
}

static int print_header(const char* name, int first) {
    if (!first && cu_puts("\n") < 0)
        return -1;

    if (cu_puts("==> ") < 0 || cu_puts(name) < 0)
        return -1;

    return cu_puts(" <==\n");
}

static int parse_count(const char* value, unsigned long* lines) {
    if (*value == '\0' || value[0] == '-')
        return -1;

    errno = 0;

    char* end;

    *lines = strtoul(value, &end, 10);

    return errno == ERANGE || *end != '\0' ? -1 : 0;
}

static int parse_options(int argc, char** argv, int* operand, unsigned long* lines,
                         int* headers) {
    while (*operand < argc) {
        const char* option = argv[*operand];

        if (option[0] != '-' || option[1] == '\0')
            break;

        if (option[1] == '-') {
            if (option[2] == '\0') {
                ++*operand;

                break;
            }

            if (cu_arg_is(option, 0, "--help"))
                return HEAD_OPTIONS_HELP;

            if (cu_arg_is(option, 0, "--version"))
                return HEAD_OPTIONS_VERSION;

            if (cu_arg_is(option, 0, "--quiet") || cu_arg_is(option, 0, "--silent"))
                *headers = 0;
            else if (cu_arg_is(option, 0, "--verbose"))
                *headers = 1;
            else
                return HEAD_OPTIONS_INVALID;

            ++*operand;

            continue;
        }

        for (int index = 1; option[index] != '\0'; ++index) {
            char flag = option[index];
            if (flag == 'h')
                return HEAD_OPTIONS_HELP;

            if (flag == 'q') {
                *headers = 0;
            }
            else if (flag == 'v') {
                *headers = 1;
            }
            else if (flag == 'n') {
                const char* value = option + index + 1;

                if (*value == '\0') {
                    if (*operand + 1 >= argc)
                        return HEAD_OPTIONS_INVALID;

                    value = argv[++*operand];
                }

                if (parse_count(value, lines) != 0)
                    return HEAD_OPTIONS_INVALID;

                break;
            }
            else {
                return HEAD_OPTIONS_INVALID;
            }
        }

        ++*operand;
    }

    return HEAD_OPTIONS_OK;
}

int main(int argc, char** argv) {
    unsigned long lines   = HEAD_DEFAULT_LINES;
    int           headers = -1;
    int           operand = 1;

    switch (parse_options(argc, argv, &operand, &lines, &headers)) {
    case HEAD_OPTIONS_HELP:
        return cu_help("head", "[-n NUMBER] [-qv] [FILE]...",
                       "Print the first 10 lines of each FILE; with more than one "
                       "FILE, a header names each file.");
    case HEAD_OPTIONS_VERSION:
        return cu_version("head");
    case HEAD_OPTIONS_INVALID:
        return cu_usage("head", "[-n NUMBER] [-qv] [FILE]...");
    default:
        break;
    }

    if (headers < 0)
        headers = argc - operand > 1;

    int first_file = 1;
    int status     = 0;

    if (operand == argc) {
        if (headers && print_header("standard input", first_file) < 0)
            return 1;

        return copy_lines(CU_STDIN, "standard input", lines);
    }

    for (int index = operand; index < argc; ++index) {
        const char* name  = argv[index];
        const char* label = name;

        int fd = CU_STDIN;

        if (__builtin_strcmp(name, "-") == 0)
            label = "standard input";
        else
            fd = open(name, O_RDONLY);

        if (fd < 0) {
            status |= cu_error("head", name);

            continue;
        }

        if (headers && print_header(label, first_file) < 0)
            status |= 1;
        else
            status |= copy_lines(fd, label, lines);

        first_file = 0;

        if (fd != CU_STDIN)
            status |= cu_close(fd, "head", name);
    }

    return status;
}
