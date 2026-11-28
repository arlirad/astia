#include "units.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { UNIT_LINE_MAX = 512 };

enum unit_key {
    KEY_NONE,
    KEY_DESCRIPTION,
    KEY_EXEC,
    KEY_ARGS,
    KEY_RESTART,
    KEY_RESTART_LIMIT,
    KEY_ENVIRONMENT,
    KEY_WORKING_DIRECTORY
};

struct parser {
    struct unit* unit;
    const char*  name;
    int          line_number;
    int          mode;
    int          exec_set;
    int          restart_set;
    int          limit_set;
    int          working_directory_set;
};

static char* trim(char* text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }

    char* end = text + strlen(text);

    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }

    *end = '\0';

    return text;
}

static void strip_comment(char* text) {
    char quote = '\0';

    for (char* cursor = text; *cursor; ++cursor) {
        if (quote) {
            if (*cursor == quote)
                quote = '\0';

            continue;
        }

        if (*cursor == '"' || *cursor == '\'') {
            quote = *cursor;

            continue;
        }

        if (*cursor == '#') {
            *cursor = '\0';

            return;
        }
    }
}

static char* unquote(char* text) {
    const size_t length = strlen(text);

    if (length >= 2 && ((text[0] == '"' && text[length - 1] == '"') ||
                        (text[0] == '\'' && text[length - 1] == '\''))) {
        ++text;

        text[length - 2] = '\0';
    }

    return text;
}

static int scalar(char* text, char* out, size_t capacity) {
    text = unquote(trim(text));

    if (!*text || strlen(text) >= capacity)
        return -1;

    strcpy(out, text);

    return 0;
}

static int parse_restart(const char* value, struct unit* unit) {
    if (!strcmp(value, "no")) {
        unit->restart = 0;

        return 0;
    }

    if (!strcmp(value, "always")) {
        unit->restart = 1;

        return 0;
    }

    if (!strcmp(value, "on-failure")) {
        unit->restart      = 1;
        unit->failure_only = 1;

        return 0;
    }

    return -1;
}

static int parse_restart_limit(const char* value, struct unit* unit) {
    char* end = NULL;

    long limit = strtol(value, &end, 10);

    if (*value == '\0' || *end != '\0' || limit < 0 || limit > 16)
        return -1;

    unit->max_restarts = (int) limit;

    return 0;
}

static enum unit_key key_for(const char* key) {
    if (!strcmp(key, "description"))
        return KEY_DESCRIPTION;

    if (!strcmp(key, "exec"))
        return KEY_EXEC;

    if (!strcmp(key, "args"))
        return KEY_ARGS;

    if (!strcmp(key, "restart"))
        return KEY_RESTART;

    if (!strcmp(key, "restart-limit"))
        return KEY_RESTART_LIMIT;

    if (!strcmp(key, "environment"))
        return KEY_ENVIRONMENT;

    if (!strcmp(key, "working-directory"))
        return KEY_WORKING_DIRECTORY;

    return KEY_NONE;
}

static int fail(struct parser* parser, const char* message) {
    fprintf(stderr, "daemond: unit %s:%d: %s\n", parser->name, parser->line_number,
            message);

    return -1;
}

static int parse_top_level(struct parser* parser, char* text) {
    char* colon = strchr(text, ':');
    if (!colon)
        return fail(parser, "expected 'key: value'");

    *colon = '\0';

    char* key   = trim(text);
    char* value = trim(colon + 1);

    switch (key_for(key)) {
    case KEY_DESCRIPTION:
        parser->mode = UNIT_MODE_PROSE;

        return 0;
    case KEY_EXEC:
        if (parser->exec_set)
            return fail(parser, "duplicate 'exec'");

        if (scalar(value, parser->unit->exec, sizeof parser->unit->exec) < 0)
            return fail(parser, "malformed 'exec'");

        parser->exec_set = 1;
        parser->mode     = UNIT_MODE_PROSE;

        return 0;
    case KEY_ARGS:
        if (*value)
            return fail(parser, "'args' must be a block list");

        parser->mode = UNIT_MODE_ARGS;

        return 0;
    case KEY_RESTART:
        if (parse_restart(value, parser->unit) < 0)
            return fail(parser, "restart must be no, always or on-failure");

        parser->restart_set = 1;
        parser->mode        = UNIT_MODE_PROSE;

        return 0;
    case KEY_RESTART_LIMIT:
        if (parse_restart_limit(value, parser->unit) < 0)
            return fail(parser, "restart-limit must be 0..16");

        parser->limit_set = 1;
        parser->mode      = UNIT_MODE_PROSE;

        return 0;
    case KEY_ENVIRONMENT:
        if (*value)
            return fail(parser, "'environment' must be a block map");

        parser->mode = UNIT_MODE_ENVIRONMENT;

        return 0;
    case KEY_WORKING_DIRECTORY:
        if (scalar(value, parser->unit->working_directory,
                   sizeof parser->unit->working_directory) < 0)
            return fail(parser, "malformed 'working-directory'");

        parser->working_directory_set = 1;
        parser->mode                  = UNIT_MODE_PROSE;

        return 0;
    case KEY_NONE:
    default:
        return fail(parser, "unknown key");
    }
}

static int parse_args_item(struct parser* parser, char* item) {
    if (*item != '-')
        return fail(parser, "expected a list item");

    item = trim(item + 1);

    if (parser->unit->argc == UNIT_ARG_MAX)
        return fail(parser, "too many args");

    if (scalar(item, parser->unit->arg_storage[parser->unit->argc],
               sizeof parser->unit->arg_storage[parser->unit->argc]) < 0)
        return fail(parser, "malformed args item");

    ++parser->unit->argc;

    return 0;
}

static int parse_environment(struct parser* parser, char* text) {
    char* colon = strchr(text, ':');
    if (!colon)
        return fail(parser, "expected 'NAME: value'");

    *colon = '\0';

    char* name  = trim(text);
    char* value = unquote(trim(colon + 1));

    if (!*name || strchr(name, '=') || strchr(name, ' '))
        return fail(parser, "malformed environment name");

    if (parser->unit->envc == UNIT_ENV_MAX)
        return fail(parser, "too many environment entries");

    if (snprintf(parser->unit->env_storage[parser->unit->envc],
                 sizeof parser->unit->env_storage[parser->unit->envc], "%s=%s", name,
                 value) >= (int) sizeof parser->unit->env_storage[parser->unit->envc])
        return fail(parser, "environment entry too long");

    ++parser->unit->envc;

    return 0;
}

void unit_rebind(struct unit* unit) {
    unit->argv[0] = unit->exec;

    for (int i = 0; i < unit->argc; ++i) {
        unit->argv[i + 1] = unit->arg_storage[i];
    }

    unit->argv[unit->argc + 1] = NULL;

    for (int i = 0; i < unit->envc; ++i) {
        unit->envp[i] = unit->env_storage[i];
    }

    unit->envp[unit->envc] = NULL;
}

static int parse_unit(const char* path, const char* name, struct unit* unit) {
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "daemond: unit %s: open failed: errno=%d\n", name, errno);

        return -1;
    }

    memset(unit, 0, sizeof *unit);

    if (strlen(name) >= sizeof unit->name) {
        fclose(file);

        fprintf(stderr, "daemond: unit %s: name too long\n", name);

        return -1;
    }

    strcpy(unit->name, name);

    struct parser parser = {
        .unit = unit, .name = name, .line_number = 0, .mode = UNIT_MODE_PROSE};

    char line[UNIT_LINE_MAX];

    while (fgets(line, sizeof line, file)) {
        ++parser.line_number;

        size_t length = strlen(line);

        if (length == sizeof line - 1 && line[length - 1] != '\n' && !feof(file)) {
            fclose(file);

            return fail(&parser, "line too long");
        }

        if (length && line[length - 1] == '\n')
            line[--length] = '\0';

        if (length && line[length - 1] == '\r')
            line[--length] = '\0';

        strip_comment(line);

        int indent = 0;

        char* cursor = line;

        while (*cursor == ' ' || *cursor == '\t') {
            if (*cursor == '\t') {
                fclose(file);

                return fail(&parser, "tabs are not supported");
            }

            ++cursor;
            ++indent;
        }

        char* text = trim(cursor);

        if (!*text)
            continue;

        int result = 0;

        if (indent == 0)
            result = parse_top_level(&parser, text);
        else if (parser.mode == UNIT_MODE_ARGS)
            result = parse_args_item(&parser, text);
        else if (parser.mode == UNIT_MODE_ENVIRONMENT)
            result = parse_environment(&parser, text);
        else
            result = fail(&parser, "unexpected indented value");

        if (result < 0) {
            fclose(file);

            return -1;
        }
    }

    fclose(file);

    if (!parser.exec_set) {
        fprintf(stderr, "daemond: unit %s: missing 'exec'\n", name);

        return -1;
    }

    if (unit->exec[0] != '/') {
        fprintf(stderr, "daemond: unit %s: 'exec' must be absolute\n", name);

        return -1;
    }

    if (unit->working_directory[0] && unit->working_directory[0] != '/') {
        fprintf(stderr, "daemond: unit %s: 'working-directory' must be absolute\n", name);

        return -1;
    }

    unit_rebind(unit);

    if (unit->restart && !parser.limit_set)
        unit->max_restarts = UNIT_DEFAULT_RESTARTS;

    return 0;
}

static int compare_units(const void* left, const void* right) {
    const struct unit* a = left;
    const struct unit* b = right;

    return strcmp(a->name, b->name);
}

int units_load(const char* directory, struct unit* units, int capacity, int* count) {
    *count = 0;

    DIR* handle = opendir(directory);

    if (!handle) {
        if (errno == ENOENT) {
            fprintf(stderr, "daemond: no unit directory %s; starting with no services\n",
                    directory);

            return 0;
        }

        fprintf(stderr, "daemond: open unit directory %s failed: errno=%d\n", directory,
                errno);

        return -1;
    }

    struct dirent* entry = NULL;

    while ((entry = readdir(handle)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        const size_t length = strlen(entry->d_name);

        if (length <= 5 || strcmp(entry->d_name + length - 5, ".yaml"))
            continue;

        if (*count == capacity) {
            closedir(handle);

            fprintf(stderr, "daemond: too many units in %s\n", directory);

            return -1;
        }

        char path[UNIT_PATH_MAX];
        char name[UNIT_NAME_MAX];

        if (snprintf(path, sizeof path, "%s/%s", directory, entry->d_name) >=
            (int) sizeof path) {
            closedir(handle);

            fprintf(stderr, "daemond: unit path too long: %s\n", entry->d_name);

            return -1;
        }

        const size_t name_length = length - 5;

        if (name_length >= sizeof name) {
            closedir(handle);

            fprintf(stderr, "daemond: unit name too long: %s\n", entry->d_name);

            return -1;
        }

        memcpy(name, entry->d_name, name_length);

        name[name_length] = '\0';

        if (parse_unit(path, name, &units[*count]) < 0) {
            closedir(handle);

            return -1;
        }

        ++*count;
    }

    closedir(handle);

    qsort(units, (size_t) *count, sizeof *units, compare_units);

    for (int i = 0; i < *count; ++i) {
        unit_rebind(&units[i]);
    }

    return 0;
}
