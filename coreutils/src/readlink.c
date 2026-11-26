#include "common.h"

#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    char buffer[256];
    long n;

    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("readlink", "LINK", "Print a symbolic link target.");

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("readlink");

    if (argc != 2)
        return cu_usage("readlink", "LINK");

    n = readlink(argv[1], buffer, sizeof buffer - 1);
    if (n < 0)
        return cu_error("readlink", strerror(errno));

    buffer[n] = '\0';

    if (cu_puts(buffer) < 0 || cu_puts("\n") < 0)
        return 1;

    return 0;
}
