#include <errno.h>
#include <sys/prctl.h>

int prctl(int option, ...) {
    (void) option;
    errno = ENOSYS;
    return -1;
}
