#include <astia/api/syscalls.h>

#include "process_port.h"
#include <sys/utsname.h>

#include <errno.h>
#include <stddef.h>

_Static_assert(sizeof(struct utsname) == sizeof(astia_utsname), "utsname layout");
_Static_assert(offsetof(struct utsname, sysname) == offsetof(astia_utsname, sysname),
               "utsname sysname offset");
_Static_assert(offsetof(struct utsname, nodename) == offsetof(astia_utsname, nodename),
               "utsname nodename offset");
_Static_assert(offsetof(struct utsname, release) == offsetof(astia_utsname, release),
               "utsname release offset");
_Static_assert(offsetof(struct utsname, version) == offsetof(astia_utsname, version),
               "utsname version offset");
_Static_assert(offsetof(struct utsname, machine) == offsetof(astia_utsname, machine),
               "utsname machine offset");

int uname(struct utsname* name) {
    long rc = __astia_process_uname(name);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
