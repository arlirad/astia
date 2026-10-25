#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("false", "", "Do nothing, unsuccessfully.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("false");
    return 1;
}
