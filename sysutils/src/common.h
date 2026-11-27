#pragma once

/*
 * astia sysutils — shared module-tool helpers.
 *
 * These tools are Astia-specific: they wrap the module-management syscalls
 * (modopen/modls/modstat/modclose), which have no portable C equivalent.
 */

#include <astia/api/syscalls.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SU_EXIT_FAILURE 1
#define SU_EXIT_USAGE 2

static inline int su_usage(const char* tool, const char* synopsis) {
    if (synopsis[0] != '\0')
        fprintf(stderr, "usage: %s %s\n", tool, synopsis);
    else
        fprintf(stderr, "usage: %s\n", tool);

    return SU_EXIT_USAGE;
}

static inline int su_help(const char* tool, const char* synopsis, const char* details) {
    printf("Usage: %s %s\n\n%s\n", tool, synopsis, details);

    return 0;
}

static inline int su_version(const char* tool) {
    printf("%s (Astia sysutils) 0.1\n", tool);

    return 0;
}

static inline int su_fail(const char* tool, const char* what, long error) {
    fprintf(stderr, "%s: %s: %s\n", tool, what, strerror((int) -error));

    return SU_EXIT_FAILURE;
}

/* Parse `slot:generation`.  A bare slot is rejected: handles are stable only
 * when the generation is checked too. */
static inline int su_parse_id(const char* text, astia_module_id* out) {
    char* end = NULL;

    unsigned long      slot;
    unsigned long long generation;

    errno = 0;
    slot  = strtoul(text, &end, 10);

    if (end == text || errno != 0 || *end != ':' || slot >= ASTIA_MODULE_SLOT_NONE)
        return 0;

    const char* generation_text = end + 1;

    errno      = 0;
    generation = strtoull(generation_text, &end, 10);

    if (end == generation_text || errno != 0 || *end != '\0')
        return 0;

    out->slot       = (uint32_t) slot;
    out->reserved   = 0;
    out->generation = generation;

    return 1;
}

static inline long su_list(astia_module_record* records, astia_module_list_info* info) {
    return astia_modls(records, ASTIA_MODULE_LIST_MAX, info);
}

/* Resolve `slot:generation` or an exact module name.
 * Returns 1 on success, 0 when no module matches, -1 on a syscall error. */
static inline int su_resolve(const char* tool, const char* text, astia_module_id* out) {
    if (su_parse_id(text, out))
        return 1;

    astia_module_record    records[ASTIA_MODULE_LIST_MAX];
    astia_module_list_info info = {0};

    const long rc = su_list(records, &info);

    if (rc < 0) {
        (void) su_fail(tool, "module list", rc);

        return -1;
    }

    int matches = 0;

    for (uint32_t i = 0; i < info.count; ++i) {
        if (strcmp(records[i].name, text) == 0) {
            *out = records[i].id;

            ++matches;
        }
    }

    if (matches == 0) {
        fprintf(stderr, "%s: %s: no such module\n", tool, text);

        return 0;
    }

    if (matches > 1) {
        fprintf(stderr, "%s: %s: ambiguous module name\n", tool, text);

        return 0;
    }

    return 1;
}

static inline const char* su_state_name(uint32_t state) {
    switch (state) {
    case ASTIA_MODULE_STATE_LOADED:
        return "loaded";
    case ASTIA_MODULE_STATE_RUNNING:
        return "running";
    case ASTIA_MODULE_STATE_EXITING:
        return "exiting";
    default:
        return "unknown";
    }
}

static inline void su_append(char* output, size_t capacity, const char* text) {
    const size_t used = strlen(output);

    if (used >= capacity - 1)
        return;

    snprintf(output + used, capacity - used, "%s%s", used == 0 ? "" : ",", text);
}

static inline void su_flags_name(uint32_t flags, char* output, size_t capacity) {
    if (capacity == 0)
        return;

    output[0] = '\0';

    if ((flags & ASTIA_MODULE_FLAG_RUNNING) != 0)
        su_append(output, capacity, "running");

    if ((flags & ASTIA_MODULE_FLAG_INITIALIZED) != 0)
        su_append(output, capacity, "initialized");

    if ((flags & ASTIA_MODULE_FLAG_RESIDENT) != 0)
        su_append(output, capacity, "resident");

    if ((flags & ASTIA_MODULE_FLAG_QUIESCING) != 0)
        su_append(output, capacity, "quiescing");

    if ((flags & ASTIA_MODULE_FLAG_UNLOADABLE) != 0)
        su_append(output, capacity, "unloadable");

    if (output[0] == '\0')
        snprintf(output, capacity, "none");
}
