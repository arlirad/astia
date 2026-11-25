#include "common.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>

static const char* group_name(gid_t gid, char* fallback) {
    struct group* group = getgrgid(gid);
    if (group != NULL)
        return group->gr_name;
    snprintf(fallback, 24, "%u", (unsigned) gid);
    return fallback;
}

int main(int argc, char** argv) {
    struct passwd* user;
    struct group*  group;
    gid_t          groups[16];
    int            count;
    if (argc > 1) {
        if (cu_is_help(argv[1]))
            return cu_help("id", "",
                           "Print the effective UID, GID, and supplementary groups.");
        if (cu_is_version(argv[1]))
            return cu_version("id");
        return cu_usage("id", "");
    }
    user  = getpwuid((uid_t) geteuid());
    group = getgrgid((gid_t) getegid());
    if (user == NULL || group == NULL)
        return cu_error("id", "cannot resolve effective account");
    if (printf("uid=%u(%s) gid=%u(%s) groups=%u(%s)", (unsigned) geteuid(), user->pw_name,
               (unsigned) getegid(), group->gr_name, (unsigned) getegid(),
               group->gr_name) < 0)
        return 1;
    count = getgroups((int) (sizeof groups / sizeof groups[0]), groups);
    if (count < 0)
        return cu_error("id", "cannot read supplementary groups");
    for (int i = 0; i < count; ++i) {
        char fallback[24];
        if (printf(",%u(%s)", (unsigned) groups[i], group_name(groups[i], fallback)) < 0)
            return 1;
        if (i + 1 < count && printf(",") < 0)
            return 1;
    }
    return printf("\n") < 0;
}
