#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE* group_file;
static char group_line[1024];
static char group_name[256], group_passwd[256];
static char* group_members[64];
static struct group group_result;

static struct group* parse_group(char* line) {
    char* field;
    char* end;
    unsigned long id;
    size_t members = 0;

    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') return NULL;
    field = strtok(line, ":"); if (field == NULL) return NULL;
    strncpy(group_name, field, sizeof group_name - 1); group_name[sizeof group_name - 1] = '\0';
    field = strtok(NULL, ":"); if (field == NULL) return NULL;
    strncpy(group_passwd, field, sizeof group_passwd - 1); group_passwd[sizeof group_passwd - 1] = '\0';
    field = strtok(NULL, ":"); if (field == NULL) return NULL;
    id = strtoul(field, &end, 10); if (*end != '\0' && *end != '\n') return NULL;
    group_result.gr_name = group_name; group_result.gr_passwd = group_passwd;
    group_result.gr_gid = (gid_t) id;
    field = strtok(NULL, "\n");
    if (field != NULL && *field != '\0') {
        char* member = strtok(field, ",");
        while (member != NULL && members + 1 < sizeof group_members / sizeof group_members[0]) {
            group_members[members++] = member; member = strtok(NULL, ",");
        }
    }
    group_members[members] = NULL; group_result.gr_mem = group_members;
    return &group_result;
}

void setgrent(void) {
    if (group_file == NULL) group_file = fopen("/etc/group", "r");
    else rewind(group_file);
}
void endgrent(void) { if (group_file != NULL) { fclose(group_file); group_file = NULL; } }
struct group* getgrent(void) {
    if (group_file == NULL) setgrent();
    while (group_file != NULL && fgets(group_line, sizeof group_line, group_file) != NULL) {
        struct group* result = parse_group(group_line); if (result != NULL) return result;
    }
    return NULL;
}
struct group* getgrnam(const char* name) {
    struct group* result; if (name == NULL) return NULL;
    setgrent(); while ((result = getgrent()) != NULL) if (strcmp(result->gr_name, name) == 0) { endgrent(); return result; }
    endgrent(); return NULL;
}
struct group* getgrgid(gid_t gid) {
    struct group* result; setgrent();
    while ((result = getgrent()) != NULL) if (result->gr_gid == gid) { endgrent(); return result; }
    endgrent(); return NULL;
}
