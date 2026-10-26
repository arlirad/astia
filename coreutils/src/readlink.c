#include "common.h"
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    char buffer[256];
    long n;
    if (argc != 2) return cu_usage("readlink", "LINK");
    if (strcmp(argv[1], "--help") == 0) return cu_help("readlink", "LINK", "Print a symbolic link target.");
    n = readlink(argv[1], buffer, sizeof buffer - 1);
    if (n < 0) return cu_error("readlink", strerror(errno));
    buffer[n] = '\0';
    if (cu_puts(buffer) < 0 || cu_puts("\n") < 0) return 1;
    return 0;
}
