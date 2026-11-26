#include "common.h"

#include <stdlib.h>
#include <string.h>

extern char** environ;

static int compare_entries(const void* lhs, const void* rhs) {
    const char* const* left  = (const char* const*) lhs;
    const char* const* right = (const char* const*) rhs;

    return strcmp(*left, *right);
}

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("env", "[--sorted] [NAME=VALUE]... [COMMAND [ARG]...]",
                       "Print the environment, or run COMMAND with the assignments\n"
                       "applied.  Without --sorted entries keep environ order.\n"
                       "  -s, --sorted  sort entries by the whole NAME=VALUE bytes");

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("env");

    int sorted        = 0;
    int first_command = 1;

    while (first_command < argc) {
        if (strcmp(argv[first_command], "-s") == 0 ||
            strcmp(argv[first_command], "--sorted") == 0) {
            sorted = 1;

            ++first_command;

            continue;
        }

        break;
    }

    while (first_command < argc) {
        const char* assignment = argv[first_command];
        const char* equals     = 0;

        if (assignment[0] == '-')
            return cu_usage("env", "[--sorted] [NAME=VALUE]... [COMMAND [ARG]...]");

        for (const char* p = assignment; *p != '\0'; ++p) {
            if (*p == '=') {
                equals = p;

                break;
            }
        }

        if (equals == 0 || equals == assignment)
            break;

        size_t name_length = (size_t) (equals - assignment);

        char* name = (char*) malloc(name_length + 1);
        if (name == 0)
            return cu_error("env", "cannot set environment");

        memcpy(name, assignment, name_length);

        name[name_length] = '\0';

        int set_result = setenv(name, equals + 1, 1);

        free(name);

        if (set_result != 0)
            return cu_error("env", "cannot set environment");

        ++first_command;
    }

    if (first_command == argc) {
        if (environ == 0)
            return 0;

        if (!sorted) {
            for (char** item = environ; *item != 0; ++item) {
                if (cu_puts(*item) < 0 || cu_puts("\n") < 0)
                    return 1;
            }

            return 0;
        }

        size_t count = 0;

        for (char** item = environ; *item != 0; ++item) {
            ++count;
        }

        if (count == 0)
            return 0;

        char** entries = (char**) malloc(count * sizeof *entries);
        if (entries == 0)
            return cu_error("env", "cannot sort environment");

        for (size_t i = 0; i < count; ++i) {
            entries[i] = environ[i];
        }

        qsort(entries, count, sizeof *entries, compare_entries);

        for (size_t i = 0; i < count; ++i) {
            if (cu_puts(entries[i]) < 0 || cu_puts("\n") < 0) {
                free(entries);

                return 1;
            }
        }

        free(entries);

        return 0;
    }

    if (execvp(argv[first_command], &argv[first_command]) < 0)
        /* Keep the utility's established usage status for a command that
           cannot be resolved, while commands that do resolve retain their
           child's status through execvp. */
        return errno == ENOENT ? 2 : 127;

    return 127;
}
