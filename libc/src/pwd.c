#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE* passwd_file;
static char passwd_line[1024];
static char passwd_fields[7][256];
static struct passwd passwd_result;

static struct passwd* parse_passwd(char* line) {
    char* field; char* end; unsigned long values[2]; int i;
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') return NULL;
    field = strtok(line, ":");
    for (i = 0; i < 7 && field != NULL; ++i) {
        strncpy(passwd_fields[i], field, sizeof passwd_fields[i] - 1);
        passwd_fields[i][sizeof passwd_fields[i] - 1] = '\0';
        field = strtok(NULL, ":\n");
    }
    if (i != 7) return NULL;
    values[0] = strtoul(passwd_fields[2], &end, 10); if (*end != '\0') return NULL;
    values[1] = strtoul(passwd_fields[3], &end, 10); if (*end != '\0') return NULL;
    passwd_result.pw_name = passwd_fields[0]; passwd_result.pw_passwd = passwd_fields[1];
    passwd_result.pw_uid = (uid_t) values[0]; passwd_result.pw_gid = (gid_t) values[1];
    passwd_result.pw_gecos = passwd_fields[4]; passwd_result.pw_dir = passwd_fields[5];
    passwd_result.pw_shell = passwd_fields[6]; return &passwd_result;
}
void setpwent(void) { if (passwd_file == NULL) passwd_file = fopen("/etc/passwd", "r"); else rewind(passwd_file); }
void endpwent(void) { if (passwd_file != NULL) { fclose(passwd_file); passwd_file = NULL; } }
struct passwd* getpwent(void) {
    if (passwd_file == NULL) setpwent();
    while (passwd_file != NULL && fgets(passwd_line, sizeof passwd_line, passwd_file) != NULL) {
        struct passwd* result = parse_passwd(passwd_line); if (result != NULL) return result;
    }
    /* Do not leave an exhausted database descriptor in the process.  Apart
     * from leaking a descriptor, an open native FILE is copied by fork and
     * can keep an unrelated child-side I/O object alive.  setpwent() will
     * reopen it on the next enumeration. */
    endpwent();
    return NULL;
}
struct passwd* getpwnam(const char* name) {
    struct passwd* result; if (name == NULL) return NULL;
    setpwent(); while ((result = getpwent()) != NULL) if (strcmp(result->pw_name, name) == 0) { endpwent(); return result; }
    endpwent(); return NULL;
}
struct passwd* getpwuid(uid_t uid) {
    struct passwd* result; setpwent();
    while ((result = getpwent()) != NULL) if (result->pw_uid == uid) { endpwent(); return result; }
    endpwent(); return NULL;
}
