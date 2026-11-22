#include <sys/utsname.h>

#include <errno.h>
#include <string.h>

int uname(struct utsname* name) {
    if (name == NULL) {
        errno = EFAULT;
        return -1;
    }
    memset(name, 0, sizeof *name);
    errno = ENOSYS;
    return -1;
}
