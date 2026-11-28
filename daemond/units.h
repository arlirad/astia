#pragma once

#include <stddef.h>

enum {
    UNIT_NAME_MAX          = 64,
    UNIT_PATH_MAX          = 160,
    UNIT_ARG_MAX           = 16,
    UNIT_ENV_MAX           = 16,
    UNIT_VALUE_MAX         = 128,
    UNIT_DEFAULT_RESTARTS  = 3,
    UNIT_RESTART_LIMIT_MAX = 16
};

enum unit_mode { UNIT_MODE_PROSE, UNIT_MODE_ARGS, UNIT_MODE_ENVIRONMENT };

/* One /etc/daemond/<name>.yaml unit.  The struct owns every string it points
 * at, so a loaded unit has no lifetime beyond the loader's array. */
struct unit {
    char name[UNIT_NAME_MAX];
    char exec[UNIT_PATH_MAX];

    char  arg_storage[UNIT_ARG_MAX][UNIT_VALUE_MAX];
    char* argv[UNIT_ARG_MAX + 2];
    int   argc;

    char  env_storage[UNIT_ENV_MAX][UNIT_VALUE_MAX];
    char* envp[UNIT_ENV_MAX + 1];
    int   envc;

    char working_directory[UNIT_PATH_MAX];

    int restart;
    int failure_only;
    int max_restarts;
};

/* Load every *.yaml unit from `directory`, sorted by unit name.  A missing
 * directory is an empty configuration (count 0); a malformed unit is an
 * error that leaves the daemon unable to start. */
int units_load(const char* directory, struct unit* units, int capacity, int* count);

/* Rebuild argv/envp after the unit was copied or moved; the vectors point at
 * the unit's own storage and must not survive a struct relocation. */
void unit_rebind(struct unit* unit);
