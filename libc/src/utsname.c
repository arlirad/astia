#include <sys/utsname.h>

#include <errno.h>

int uname(struct utsname* name) {
    (void) name;
    errno = ENOSYS;
    return -1;
}
