#include "common.h"

#include <string.h>

static int all_slashes(const char* value) {
    if (value[0] == '\0')
        return 0;

    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor != '/')
            return 0;
    }

    return 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--") == 0) {
        ++argv;
        --argc;
    }

    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("dirname", "STRING", "Print the directory component of STRING.");

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("dirname");

    if (argc != 2)
        return cu_usage("dirname", "STRING");

    const char* value = argv[1];

    if (all_slashes(value))
        return cu_puts("/\n") < 0 ? 1 : 0;

    size_t end = strlen(value);

    while (end != 0 && value[end - 1] == '/') {
        --end;
    }

    size_t slash = end;

    while (slash != 0 && value[slash - 1] != '/') {
        --slash;
    }

    if (slash == 0)
        return cu_puts(".\n") < 0 ? 1 : 0;

    while (slash > 1 && value[slash - 1] == '/') {
        --slash;
    }

    return cu_write_all(CU_STDOUT, value, slash) < 0 || cu_puts("\n") < 0;
}
