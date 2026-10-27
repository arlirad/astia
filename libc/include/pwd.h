#pragma once

#include <sys/types.h>

/* The native account database is the traditional, line-oriented format.
 * Password fields are exposed for compatibility but are never interpreted. */
struct passwd {
    char*  pw_name;
    char*  pw_passwd;
    uid_t  pw_uid;
    gid_t  pw_gid;
    char*  pw_gecos;
    char*  pw_dir;
    char*  pw_shell;
};

struct passwd* getpwnam(const char* name);
struct passwd* getpwuid(uid_t uid);
void           setpwent(void);
struct passwd* getpwent(void);
void           endpwent(void);
