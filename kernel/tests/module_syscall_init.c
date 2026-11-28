#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

static int streq(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }

    return *a == *b;
}

static void clear_bytes(void* destination, unsigned long length) {
    unsigned char* bytes = destination;

    for (unsigned long i = 0; i < length; ++i) {
        bytes[i] = 0;
    }
}

static long list_total(unsigned long* total) {
    astia_module_list_info info;

    clear_bytes(&info, sizeof info);

    if (astia_modls((astia_module_record*) 0, 0, &info) != 0)
        return -1;

    *total = info.total;

    return 0;
}

static int record_matches(const astia_module_record* record, const astia_module_id* id) {
    return record->id.slot == id->slot && record->id.generation == id->generation;
}

void c_main(const long* frame) {
    (void) frame;

    astia_module_list_info info;
    astia_module_stat      stat;
    astia_module_id        id;
    astia_module_record    records[ASTIA_MODULE_LIST_MAX];

    unsigned long baseline = 0;
    unsigned long after    = 0;
    unsigned long i        = 0;
    int           found    = 0;

    clear_bytes(&info, sizeof info);
    clear_bytes(&stat, sizeof stat);
    clear_bytes(&id, sizeof id);
    clear_bytes(records, sizeof records);

    if (list_total(&baseline) != 0 || baseline == 0)
        astia_exit(1);

    if (astia_seteuid(1) != 0)
        astia_exit(2);

    clear_bytes(&id, sizeof id);

    if (astia_modopen("/mod/does-not-exist", ASTIA_MODULE_OPEN_NONE, &id) != -EPERM)
        astia_exit(2);

    if (astia_modls((astia_module_record*) 0, 0, &info) != -EPERM)
        astia_exit(2);

    if (astia_modstat(&id, &stat) != -EPERM)
        astia_exit(2);

    if (astia_modclose(&id) != -EPERM)
        astia_exit(2);

    if (astia_seteuid(0) != 0)
        astia_exit(3);

    {
        static const struct {
            const char* path;
            long        error;
        } failures[] = {
            {"/mod/missing.km", -ENOENT}, {"/mod", -ENOEXEC},
            {"/etc/passwd", -ENOEXEC},   {"/mod/truncated.elf", -ENOEXEC},
            {"/mod/huge.bin", -E2BIG},   {"/mod/bad-dependency.km", -ENOEXEC},
        };

        for (i = 0; i < sizeof failures / sizeof failures[0]; ++i) {
            clear_bytes(&id, sizeof id);

            if (astia_modopen(failures[i].path, ASTIA_MODULE_OPEN_NONE, &id) !=
                failures[i].error)
                astia_exit(4 + (int) i);

            if (list_total(&after) != 0 || after != baseline)
                astia_exit(10 + (int) i);
        }
    }

    clear_bytes(&id, sizeof id);

    if (astia_modopen("/mod/hello.km", 1, &id) != -EINVAL)
        astia_exit(16);

    if (astia_modopen("/mod/hello.km", ASTIA_MODULE_OPEN_NONE, &id) != 0)
        astia_exit(17);

    if (id.slot == ASTIA_MODULE_SLOT_NONE || id.reserved != 0)
        astia_exit(17);

    if (astia_modls(records, ASTIA_MODULE_LIST_MAX, &info) != 0 || info.count == 0)
        astia_exit(18);

    if (info.total != (uint32_t) (baseline + 1))
        astia_exit(18);

    for (i = 0; i < info.count; ++i) {
        if (record_matches(&records[i], &id))
            found = 1;
    }

    if (!found)
        astia_exit(18);

    if (astia_modstat(&id, &stat) != 0)
        astia_exit(19);

    if (!streq(stat.name, "hello.km"))
        astia_exit(19);

    if (stat.state != ASTIA_MODULE_STATE_LOADED || stat.image_size == 0)
        astia_exit(19);

    {
        astia_module_id first;

        clear_bytes(&first, sizeof first);

        first.slot       = id.slot;
        first.reserved   = id.reserved;
        first.generation = id.generation;

        if (astia_modclose(&first) != 0)
            astia_exit(20);

        if (astia_modstat(&first, &stat) != -ENOENT)
            astia_exit(20);

        if (list_total(&after) != 0 || after != baseline)
            astia_exit(20);

        clear_bytes(&id, sizeof id);

        if (astia_modopen("/mod/hello.km", ASTIA_MODULE_OPEN_NONE, &id) != 0)
            astia_exit(21);

        if (id.slot != first.slot || id.generation == first.generation)
            astia_exit(21);

        if (astia_modstat(&first, &stat) != -ENOENT)
            astia_exit(22);

        if (astia_modstat(&id, &stat) != 0)
            astia_exit(22);

        if (astia_modclose(&id) != 0)
            astia_exit(23);

        if (astia_modstat(&id, &stat) != -ENOENT)
            astia_exit(23);

        if (astia_modclose(&first) != -ENOENT)
            astia_exit(24);
    }

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}

__asm__(".globl _start\n"
        "_start:\n"
        "    movq %rsp, %rdi\n"
        "    call c_main\n"
        "    ud2\n");
