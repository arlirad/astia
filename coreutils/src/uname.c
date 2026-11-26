#include "common.h"
#include <sys/utsname.h>

enum {
    FIELD_SYSNAME,
    FIELD_NODENAME,
    FIELD_RELEASE,
    FIELD_VERSION,
    FIELD_MACHINE,
    FIELD_COUNT
};

#define FIELD_BIT(field) (1u << (field))
#define ALL_FIELDS (FIELD_BIT(FIELD_COUNT) - 1)

static int print_field(const char* value, int* printed) {
    if (*printed && cu_puts(" ") < 0)
        return -1;

    if (cu_puts(value) < 0)
        return -1;

    *printed = 1;

    return 0;
}

static int load_identity(struct utsname* identity, int* loaded) {
    if (*loaded)
        return 0;

    if (uname(identity) != 0)
        return cu_error("uname", "cannot read system information");

    *loaded = 1;

    return 0;
}

static int field_index(char option) {
    switch (option) {
    case 's':
        return FIELD_SYSNAME;
    case 'n':
        return FIELD_NODENAME;
    case 'r':
        return FIELD_RELEASE;
    case 'v':
        return FIELD_VERSION;
    case 'm':
        return FIELD_MACHINE;
    default:
        return -1;
    }
}

static const char* field_value(const struct utsname* identity, int field) {
    const char* values[FIELD_COUNT] = {identity->sysname, identity->nodename,
                                       identity->release, identity->version,
                                       identity->machine};

    return values[field];
}

static int print_selected(const struct utsname* identity, unsigned int selected,
                          int* printed) {
    for (int field = 0; field < FIELD_COUNT; ++field) {
        if (!(selected & FIELD_BIT(field)))
            continue;

        if (print_field(field_value(identity, field), printed) < 0)
            return -1;
    }

    return 0;
}

int main(int argc, char** argv) {
    struct utsname identity;

    unsigned int selected = 0;
    int          loaded   = 0;

    for (int i = 1; i < argc; ++i) {
        const char* option = argv[i];

        if (cu_is_help(option))
            return cu_help("uname", "[OPTION]...", "Print system information.");

        if (cu_is_version(option))
            return cu_version("uname");

        if (cu_arg_is(option, "-a", "--all")) {
            selected = ALL_FIELDS;

            continue;
        }

        if (option[0] != '-' || option[1] == '\0')
            return cu_usage("uname", "[OPTION]...");

        for (int j = 1; option[j] != '\0'; ++j) {
            if (option[j] == 'a') {
                selected = ALL_FIELDS;

                continue;
            }

            int field = field_index(option[j]);
            if (field < 0)
                return cu_error("uname", "unknown option");

            selected |= FIELD_BIT(field);
        }
    }

    if (selected == 0)
        selected = FIELD_BIT(FIELD_SYSNAME);

    if (load_identity(&identity, &loaded) != 0)
        return 1;

    int printed = 0;

    if (print_selected(&identity, selected, &printed) < 0)
        return 1;

    return cu_puts("\n") < 0;
}
