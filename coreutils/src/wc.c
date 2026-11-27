#include "common.h"
#include "read_retry.h"

#include <fcntl.h>
#include <stdio.h>

typedef struct {
    unsigned long lines;
    unsigned long words;
    unsigned long bytes;
} wc_counts;

typedef struct {
    int lines;
    int words;
    int bytes;
} wc_fields;

static void add_counts(wc_counts* total, const wc_counts* counts) {
    total->lines += counts->lines;
    total->words += counts->words;
    total->bytes += counts->bytes;
}

static int write_number(unsigned long value, int* started) {
    char buffer[32];
    int  length = snprintf(buffer, sizeof buffer, *started ? " %lu" : "%lu", value);

    if (length < 0 || cu_write_all(CU_STDOUT, buffer, (size_t) length) < 0)
        return -1;

    *started = 1;

    return 0;
}

static int write_counts(const wc_counts* counts, const wc_fields* fields,
                        const char* name) {
    int started = 0;

    if (fields->lines && write_number(counts->lines, &started) < 0)
        return -1;

    if (fields->words && write_number(counts->words, &started) < 0)
        return -1;

    if (fields->bytes && write_number(counts->bytes, &started) < 0)
        return -1;

    if (name != 0 &&
        (cu_write_all(CU_STDOUT, " ", 1) < 0 || cu_puts_fd(CU_STDOUT, name) < 0))
        return -1;

    return cu_write_all(CU_STDOUT, "\n", 1) < 0;
}

static int count_fd(int fd, const char* name, const wc_fields* fields,
                    wc_counts* counts) {
    char          buffer[4096];
    unsigned long lines = 0, words = 0, bytes = 0;
    int           in_word = 0;

    for (;;) {
        long count = cu_read_retry(fd, buffer, sizeof buffer);
        if (count == 0)
            break;

        if (count < 0)
            return cu_error("wc", name != 0 ? name : "standard input");

        bytes += (unsigned long) count;

        for (long i = 0; i < count; ++i) {
            char c = buffer[i];
            if (c == '\n')
                ++lines;

            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                in_word = 0;
            }
            else if (!in_word) {
                in_word = 1;

                ++words;
            }
        }
    }

    *counts = (wc_counts){lines, words, bytes};

    return write_counts(counts, fields, name) < 0 ? 1 : 0;
}

static int parse_options(const char* path, wc_fields* fields) {
    for (const char* option = path + 1; *option != '\0'; ++option) {
        if (*option == 'l')
            fields->lines = 1;
        else if (*option == 'w')
            fields->words = 1;
        else if (*option == 'c')
            fields->bytes = 1;
        else
            return -1;
    }

    return 0;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    wc_fields fields = {0, 0, 0};
    wc_counts totals = {0, 0, 0};

    int fields_given = 0;

    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("wc", "[OPTION]... [FILE]...",
                       "Print newline, word, and byte counts for each FILE.");

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("wc");

    cu_args options;

    cu_args_init(&options, argc, argv);

    const char* path;

    while ((path = cu_args_next(&options)) != 0) {
        if (!options.options || path[0] != '-' || path[1] == '\0')
            continue;

        if (parse_options(path, &fields) != 0)
            return cu_error("wc", "unknown option");

        fields_given = 1;
    }

    if (!fields_given)
        fields = (wc_fields){1, 1, 1};

    int status   = 0;
    int operands = 0;

    while ((path = cu_args_next(&args)) != 0) {
        if (args.options && path[0] == '-' && path[1] != '\0')
            continue;

        ++operands;

        int fd = path[1] == '\0' ? CU_STDIN : open(path, O_RDONLY);
        if (fd < 0) {
            status |= cu_error("wc", path);

            continue;
        }

        wc_counts counts;

        if (count_fd(fd, path, &fields, &counts) != 0)
            status = 1;
        else
            add_counts(&totals, &counts);

        if (fd != CU_STDIN)
            status |= cu_close(fd, "wc", path);
    }

    if (operands == 0) {
        wc_counts counts;

        return count_fd(CU_STDIN, 0, &fields, &counts);
    }

    if (operands > 1 && write_counts(&totals, &fields, "total") < 0)
        status = 1;

    return status;
}
