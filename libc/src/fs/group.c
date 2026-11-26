#include "internal/account_fields.h"

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { INITGROUPS_MAX_GROUPS = 16 };

static FILE*        group_file;
static char         group_line[1024];
static char         group_name[256], group_passwd[256];
static char*        group_members[64];
static struct group group_result;

__attribute__((weak, noinline)) const char* __astia_group_database_path(void) {
    return "/etc/group";
}

static int group_list_contains(const gid_t* groups, int count, gid_t gid) {
    int i;

    for (i = 0; i < count; ++i) {
        if (groups[i] == gid)
            return 1;
    }

    return 0;
}

static struct group* parse_group(char* line) {
    char* fields[4];
    char* end;

    size_t count;
    size_t members = 0;

    unsigned long id;

    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
        return NULL;

    count = __astia_account_field_split(line, ':', fields, 4);

    if (count < 3 || count > 4)
        return NULL;

    if (fields[0][0] == '\0' || fields[2][0] == '\0')
        return NULL;

    id = strtoul(fields[2], &end, 10);

    if (*end != '\0')
        return NULL;

    strncpy(group_name, fields[0], sizeof group_name - 1);

    group_name[sizeof group_name - 1] = '\0';

    strncpy(group_passwd, fields[1], sizeof group_passwd - 1);

    group_passwd[sizeof group_passwd - 1] = '\0';

    group_result.gr_name   = group_name;
    group_result.gr_passwd = group_passwd;
    group_result.gr_gid    = (gid_t) id;

    if (count == 4 && fields[3][0] != '\0') {
        char* member = strtok(fields[3], ",");

        while (member != NULL &&
               members + 1 < sizeof group_members / sizeof group_members[0]) {
            group_members[members++] = member;
            member                   = strtok(NULL, ",");
        }
    }

    group_members[members] = NULL;
    group_result.gr_mem    = group_members;

    return &group_result;
}

void setgrent(void) {
    if (group_file == NULL)
        group_file = fopen(__astia_group_database_path(), "r");
    else
        rewind(group_file);
}

void endgrent(void) {
    if (group_file != NULL) {
        fclose(group_file);

        group_file = NULL;
    }
}

struct group* getgrent(void) {
    if (group_file == NULL)
        setgrent();

    while (group_file != NULL &&
           fgets(group_line, sizeof group_line, group_file) != NULL) {
        struct group* result = parse_group(group_line);
        if (result != NULL)
            return result;
    }

    /* Keep exhausted account files out of subsequently forked children. */
    endgrent();

    return NULL;
}

struct group* getgrnam(const char* name) {
    struct group* result;

    if (name == NULL)
        return NULL;

    setgrent();

    while ((result = getgrent()) != NULL) {
        if (strcmp(result->gr_name, name) == 0) {
            endgrent();

            return result;
        }
    }

    endgrent();

    return NULL;
}

struct group* getgrgid(gid_t gid) {
    struct group* result;

    setgrent();

    while ((result = getgrent()) != NULL) {
        if (result->gr_gid == gid) {
            endgrent();

            return result;
        }
    }

    endgrent();

    return NULL;
}

int initgroups(const char* user, gid_t group) {
    gid_t groups[INITGROUPS_MAX_GROUPS];

    int count = 0;

    struct group* entry;

    if (user == NULL) {
        errno = EINVAL;

        return -1;
    }

    groups[count++] = group;

    setgrent();

    while ((entry = getgrent()) != NULL) {
        char** member;

        for (member = entry->gr_mem; *member != NULL; ++member) {
            if (strcmp(*member, user) != 0)
                continue;

            if (!group_list_contains(groups, count, entry->gr_gid)) {
                if (count == INITGROUPS_MAX_GROUPS) {
                    endgrent();

                    errno = EINVAL;

                    return -1;
                }

                groups[count++] = entry->gr_gid;
            }

            break;
        }
    }

    endgrent();

    return setgroups(count, groups);
}
