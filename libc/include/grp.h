#pragma once

#include <sys/types.h>

struct group {
    char*  gr_name;
    char*  gr_passwd;
    gid_t  gr_gid;
    char** gr_mem;
};

/*
 * getgrnam(), getgrgid(), and getgrent() return a pointer to static storage
 * that the next group lookup overwrites. The gr_mem array is allocated and
 * grown by the library and remains owned by it; callers must not free it, and
 * a returned entry stays valid only until the next lookup. This interface is
 * not thread-safe.
 */
struct group* getgrnam(const char* name);
struct group* getgrgid(gid_t gid);
void          setgrent(void);
struct group* getgrent(void);
void          endgrent(void);
int           initgroups(const char* user, gid_t group);
