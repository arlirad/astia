#include "common.h"

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
        return su_help("modls", "", "List the published kernel modules.");

    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return su_version("modls");

    if (argc != 1)
        return su_usage("modls", "");

    astia_module_record    records[ASTIA_MODULE_LIST_MAX] = {0};
    astia_module_list_info info                           = {0};

    const long rc = astia_modls(records, ASTIA_MODULE_LIST_MAX, &info);

    if (rc < 0)
        return su_fail("modls", "list", rc);

    if (info.count == 0) {
        printf("no modules\n");

        return 0;
    }

    printf("ID STATE FLAGS REFS DEPS SIZE NAME\n");

    for (uint32_t i = 0; i < info.count; ++i) {
        char flags[96];

        su_flags_name(records[i].flags, flags, sizeof flags);

        printf("%u:%llu %s %s refs=%u deps=%u size=%llu %s\n",
               (unsigned) records[i].id.slot,
               (unsigned long long) records[i].id.generation,
               su_state_name(records[i].state), flags, (unsigned) records[i].active_refs,
               (unsigned) records[i].dependency_count,
               (unsigned long long) records[i].image_size, records[i].name);
    }

    if (info.total > info.count)
        printf("showing %u of %u modules (list capacity %u)\n", info.count, info.total,
               info.capacity);

    return 0;
}
