#include <grp.h>

struct group* getgrgid(gid_t gid) {
    (void) gid;
    return (struct group*) 0;
}
