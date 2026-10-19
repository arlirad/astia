#include <errno.h>
#include <sys/utsname.h>

int uname(struct utsname* name) {
    (void) name;
    errno = ENOSYS;
    return -1;
}
