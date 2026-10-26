#include "common.h"
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("ln", "TARGET LINK_NAME", "Create a symbolic link.");
    if (argc == 2 && cu_is_version(argv[1])) return cu_version("ln");
    if (argc != 3) return cu_usage("ln", "TARGET LINK_NAME");
    if (strcmp(argv[1], "--help") == 0) return cu_help("ln", "TARGET LINK_NAME", "Create a symbolic link.");
    if (symlink(argv[1], argv[2]) < 0) return cu_error("ln", strerror(errno));
    return 0;
}
