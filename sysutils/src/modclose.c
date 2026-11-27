#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
        return su_help("modclose", "MODULE",
                       "Quiesce and unload a kernel module. MODULE is a module name or\n"
                       "the slot:generation token printed by modls.");

    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return su_version("modclose");

    if (argc != 2)
        return su_usage("modclose", "MODULE");

    astia_module_id resolved = {0};

    const int found = su_resolve("modclose", argv[1], &resolved);

    if (found <= 0)
        return SU_EXIT_FAILURE;

    const long rc = astia_modclose(&resolved);
    if (rc < 0)
        return su_fail("modclose", argv[1], rc);

    printf("closed %u:%llu\n", (unsigned) resolved.slot,
           (unsigned long long) resolved.generation);

    return 0;
}
