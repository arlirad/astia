#include "common.h"

extern char** environ;

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("env", "", "Print the environment.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("env");
    if (argc != 1)
        return cu_usage("env", "");
    if (environ == 0)
        return 0;
    for (char** item = environ; *item != 0; ++item) {
        if (cu_puts(*item) < 0 || cu_puts("\n") < 0)
            return 1;
    }
    return 0;
}
