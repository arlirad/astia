#pragma once

#include "common.h"

#include <astia/api/syscalls.h>

#include <errno.h>
#include <string.h>

static int cu_power_transition(const char* name, uint32_t operation) {
    long result = astia_power(operation);
    if (result < 0) {
        errno = (int) -result;

        return cu_error(name, strerror(errno));
    }

    return cu_error(name, "power transition returned unexpectedly");
}

static int cu_power_main(int argc, char** argv, const char* name, const char* details,
                         uint32_t operation) {
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help(name, "", details);

    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version(name);

    if (argc != 1)
        return cu_usage(name, "");

    return cu_power_transition(name, operation);
}
