#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
        return su_help("modstat", "MODULE",
                       "Show one kernel module snapshot. MODULE is a module name or\n"
                       "the slot:generation token printed by modls.");

    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return su_version("modstat");

    if (argc != 2)
        return su_usage("modstat", "MODULE");

    astia_module_id resolved = {0};

    const int found = su_resolve("modstat", argv[1], &resolved);

    if (found <= 0)
        return SU_EXIT_FAILURE;

    astia_module_stat stat = {0};

    const long rc = astia_modstat(&resolved, &stat);

    if (rc < 0)
        return su_fail("modstat", argv[1], rc);

    char flags[96];

    su_flags_name(stat.flags, flags, sizeof flags);

    printf("name: %s\n", stat.name);
    printf("id: %u:%llu\n", (unsigned) stat.id.slot,
           (unsigned long long) stat.id.generation);
    printf("state: %s\n", su_state_name(stat.state));
    printf("flags: %s\n", flags);
    printf("image-size: %llu\n", (unsigned long long) stat.image_size);
    printf("active-refs: %u\n", (unsigned) stat.active_refs);
    printf("dependents: %u\n", (unsigned) stat.dependent_count);
    printf("dependencies: %u\n", (unsigned) stat.dependency_count);

    for (uint32_t i = 0; i < stat.dependencies_written; ++i) {
        printf("  %u:%llu\n", (unsigned) stat.dependencies[i].id.slot,
               (unsigned long long) stat.dependencies[i].id.generation);
    }

    return 0;
}
