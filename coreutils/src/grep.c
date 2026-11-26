#include "common.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int grep_fd(int fd, const char* name, const char* pattern, int invert,
                   int numbered, int insensitive) {
    char          line[4096];
    unsigned long number  = 0;
    int           matched = 0;

    for (;;) {
        long length   = 0;
        int  complete = 0;

        while (!complete && length < (long) sizeof line - 1) {
            char c;
            long count = read(fd, &c, 1);
            if (count < 0)
                return cu_error("grep", name);

            if (count == 0) {
                complete = 1;

                break;
            }

            line[length++] = c;

            if (c == '\n')
                complete = 1;
        }

        if (length == 0 && complete)
            break;

        line[length] = '\0';

        ++number;

        int found = 0;

        if (!insensitive) {
            found = strstr(line, pattern) != 0;
        }
        else {
            size_t pattern_length = strlen(pattern);
            if (pattern_length == 0)
                found = 1;

            for (size_t start = 0; line[start] != '\0' && !found; ++start) {
                size_t i = 0;

                while (i < pattern_length && line[start + i] != '\0' &&
                       tolower((unsigned char) line[start + i]) ==
                           tolower((unsigned char) pattern[i])) {
                    ++i;
                }

                if (i == pattern_length)
                    found = 1;
            }
        }

        if ((found != 0) == (invert == 0)) {
            matched = 1;

            if (numbered) {
                char prefix[32];
                int  n = snprintf(prefix, sizeof prefix, "%lu:", number);

                if (cu_write_all(CU_STDOUT, prefix, (size_t) n) < 0)
                    return 2;
            }

            if (name != 0 && name[0] != '\0') {
                if (cu_puts_fd(CU_STDOUT, name) < 0 || cu_puts_fd(CU_STDOUT, ":") < 0)
                    return 2;
            }

            if (cu_write_all(CU_STDOUT, line, (size_t) length) < 0)
                return 2;

            if (line[length - 1] != '\n' && cu_puts_fd(CU_STDOUT, "\n") < 0)
                return 2;
        }

        if (!complete) {
            do {
                char c;
                long count = read(fd, &c, 1);
                if (count < 0)
                    return cu_error("grep", name);

                if (count == 0 || c == '\n') {
                    complete = 1;

                    break;
                }
            } while (1);
        }
    }

    return matched ? 0 : 1;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    const char* pattern = 0;

    int invert = 0, numbered = 0, insensitive = 0;

    const char* value;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("grep", "[-i] [-n] [-v] PATTERN [FILE]...",
                           "Print lines matching PATTERN; - means standard input.");

        if (cu_is_version(value))
            return cu_version("grep");

        if (args.options && __builtin_strcmp(value, "-n") == 0) {
            numbered = 1;

            continue;
        }

        if (args.options && __builtin_strcmp(value, "-i") == 0) {
            insensitive = 1;

            continue;
        }

        if (args.options && __builtin_strcmp(value, "-v") == 0) {
            invert = 1;

            continue;
        }

        if (args.options && value[0] == '-')
            return cu_error("grep", "unknown option");

        if (pattern == 0) {
            pattern = value;

            break;
        }
    }

    if (pattern == 0)
        return cu_usage("grep", "[-i] [-n] [-v] PATTERN [FILE]...");

    int status = 1;
    int files  = args.index < argc;
    if (!files)
        return grep_fd(CU_STDIN, "", pattern, invert, numbered, insensitive);

    for (int i = args.index; i < argc; ++i) {
        int fd = __builtin_strcmp(argv[i], "-") == 0 ? CU_STDIN : open(argv[i], O_RDONLY);
        if (fd < 0) {
            cu_error("grep", argv[i]);

            status = 2;

            continue;
        }

        int result = grep_fd(fd, files && argc - args.index > 1 ? argv[i] : "", pattern,
                             invert, numbered, insensitive);

        if (fd != CU_STDIN)
            close(fd);

        if (result == 0)
            status = 0;
        else if (result == 2)
            status = 2;
    }

    return status;
}
