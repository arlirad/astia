#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("true", "", "Do nothing, successfully.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("true");
    return 0;
}
