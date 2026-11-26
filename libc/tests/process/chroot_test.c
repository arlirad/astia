/*
 * chroot(2) is a thin translation of the native process seam: the wrapper
 * must forward the path unchanged and map a negated errno to -1/errno.
 */
#include "internal/process_port.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int         fails;
static const char* last_path;
static long        next_result;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

long __astia_process_chroot(const char* path) {
    last_path = path;

    if (path == NULL)
        return -EFAULT;

    return next_result;
}

int main(void) {
    last_path   = NULL;
    next_result = 0;

    CHECK(chroot("/jail") == 0);
    CHECK(last_path != NULL && strcmp(last_path, "/jail") == 0);

    next_result = -EPERM;
    errno       = 0;

    CHECK(chroot("/jail") == -1);
    CHECK(errno == EPERM);

    next_result = -ENOENT;
    errno       = 0;

    CHECK(chroot("/missing") == -1);
    CHECK(errno == ENOENT);

    errno = 0;

    CHECK(chroot(NULL) == -1);
    CHECK(errno == EFAULT);

    return fails == 0 ? 0 : 1;
}
