#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
        return su_help("modopen", "PATH",
                       "Load and publish the kernel module image at PATH.\n"
                       "Prints the new slot:generation token on success.");

    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return su_version("modopen");

    if (argc != 2)
        return su_usage("modopen", "PATH");

    astia_module_id id = {0};

    const long rc = astia_modopen(argv[1], ASTIA_MODULE_OPEN_NONE, &id);

    if (rc < 0)
        return su_fail("modopen", argv[1], rc);

    printf("%u:%llu\n", (unsigned) id.slot, (unsigned long long) id.generation);

    return 0;
}
