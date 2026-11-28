#include "common.h"
#include "sort_growth.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static int reverse_order;
static int unique_lines;

static void free_lines(char** lines, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        free(lines[i]);
    }

    free(lines);
}

static int compare_lines(const void* left, const void* right) {
    int result = strcmp(*(const char* const*) left, *(const char* const*) right);

    return reverse_order ? -result : result;
}

static int sort_fd(int fd, const char* name, char*** lines, size_t* count,
                   size_t* capacity) {
    cu_line_reader reader;

    cu_line_reader_init(&reader, fd);

    for (;;) {
        int status = cu_read_line(&reader);
        if (status == 0)
            break;

        if (status < 0) {
            cu_line_reader_destroy(&reader);

            return status == -2 ? cu_error("sort", "out of memory")
                                : cu_error("sort", name);
        }

        char* line = (char*) malloc(reader.line_length + 1);
        if (line == 0) {
            cu_line_reader_destroy(&reader);

            return cu_error("sort", "out of memory");
        }

        memcpy(line, reader.line, reader.line_length + 1);

        if (*count == *capacity) {
            if (cu_sort_grow_lines(lines, capacity, realloc) != 0) {
                free(line);
                cu_line_reader_destroy(&reader);

                return cu_error("sort", "out of memory");
            }
        }

        (*lines)[(*count)++] = line;
    }

    cu_line_reader_destroy(&reader);

    return 0;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    char** lines = 0;

    size_t count = 0, capacity = 0, operands = 0;

    int status = 0;

    const char* value;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help(
                "sort", "[-r] [-u] [FILE]...",
                "Sort lines of text; read standard input when no FILE is given.");

        if (cu_is_version(value))
            return cu_version("sort");

        if (args.options && __builtin_strcmp(value, "-r") == 0) {
            reverse_order = 1;

            continue;
        }

        if (args.options && __builtin_strcmp(value, "-u") == 0) {
            unique_lines = 1;

            continue;
        }

        if (args.options && value[0] == '-' && value[1] != '\0')
            return cu_error("sort", "unknown option");

        ++operands;

        int fd = __builtin_strcmp(value, "-") == 0 ? CU_STDIN : open(value, O_RDONLY);
        if (fd < 0) {
            cu_error("sort", value);

            return 1;
        }

        if (sort_fd(fd, value, &lines, &count, &capacity) != 0) {
            if (fd != CU_STDIN)
                cu_close(fd, "sort", value);

            free_lines(lines, count);

            return 1;
        }

        if (fd != CU_STDIN && cu_close(fd, "sort", value) != 0)
            status = 1;
    }

    if (operands == 0) {
        if (sort_fd(CU_STDIN, "standard input", &lines, &count, &capacity) != 0) {
            free_lines(lines, count);

            return 1;
        }
    }

    if (count > 1)
        qsort(lines, count, sizeof(char*), compare_lines);

    char* previous = 0;

    for (size_t i = 0; i < count; ++i) {
        if (unique_lines && previous != 0 && strcmp(lines[i], previous) == 0) {
            free(lines[i]);
            continue;
        }

        if (cu_write_all(CU_STDOUT, lines[i], strlen(lines[i])) < 0) {
            free(previous);
            free(lines[i]);

            for (size_t j = i + 1; j < count; ++j) {
                free(lines[j]);
            }

            free(lines);

            return 1;
        }

        free(previous);

        previous = lines[i];
    }

    free(previous);
    free(lines);

    return status;
}
