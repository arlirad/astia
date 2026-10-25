#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("pwd", "", "Print the name of the current working directory.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("pwd");
    if (argc != 1)
        return cu_usage("pwd", "");
    char cwd[256];
    if (getcwd(cwd, sizeof cwd) == 0)
        return cu_error("pwd", "cannot read current directory");
    return cu_puts(cwd) < 0 || cu_puts("\n") < 0;
}
