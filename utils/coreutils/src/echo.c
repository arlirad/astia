#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("echo", "[-n] [STRING]...",
                       "Write the arguments to standard output.");

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("echo");

    int first   = 1;
    int newline = 1;

    if (first < argc && argv[first][0] == '-' && argv[first][1] == 'n' &&
        argv[first][2] == '\0') {
        newline = 0;

        ++first;
    }

    for (int i = first; i < argc; ++i) {
        if (i != first && cu_puts(" ") < 0)
            return 1;

        if (cu_puts(argv[i]) < 0)
            return 1;
    }

    if (newline && cu_puts("\n") < 0)
        return 1;

    return 0;
}
