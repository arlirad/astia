#include "common.h"

#include <string.h>

static int all_slashes(const char* value) {
    if (value[0] == '\0')
        return 0;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
        if (*cursor != '/')
            return 0;
    return 1;
}

int main(int argc, char** argv) {
    const char* value;
    const char* suffix = 0;
    if (argc > 1 && strcmp(argv[1], "--") == 0) {
        ++argv;
        --argc;
    }
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("basename", "STRING [SUFFIX]", "Print the final component of STRING.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("basename");
    if (argc != 2 && argc != 3)
        return cu_usage("basename", "STRING [SUFFIX]");
    value = argv[1];
    if (argc == 3)
        suffix = argv[2];
    if (value[0] == '\0')
        return cu_puts("\n") < 0 ? 1 : 0;
    if (all_slashes(value))
        return cu_puts("/\n") < 0 ? 1 : 0;

    size_t end = strlen(value);
    while (end != 0 && value[end - 1] == '/')
        --end;
    size_t start = end;
    while (start != 0 && value[start - 1] != '/')
        --start;
    size_t length = end - start;
    if (suffix != 0) {
        size_t suffix_length = strlen(suffix);
        if (suffix_length < length && memcmp(value + start + length - suffix_length,
                                             suffix, suffix_length) == 0)
            length -= suffix_length;
    }
    return cu_write_all(CU_STDOUT, value + start, length) < 0 || cu_puts("\n") < 0;
}
