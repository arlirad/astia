#include <astia/api/syscalls.h>

#include <sys/resource.h>

#include <errno.h>

int getrlimit(int resource, struct rlimit* limit) {
    long rc = astia_getrlimit((int32_t) resource, (astia_rlimit*) limit);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return 0;
}

int setrlimit(int resource, const struct rlimit* limit) {
    long rc = astia_setrlimit((int32_t) resource, (const astia_rlimit*) limit);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return 0;
}

/* zsh's rlimits module seam. */
void set_resinfo(void) {}

void free_resinfo(void) {}
