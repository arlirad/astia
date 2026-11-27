#include "mode.h"

#include <dlfcn.h>

#define LD_RTLD_SUPPORTED (RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE)

int ld_mode_valid(int mode) {
    if (mode < 0 || (mode & ~LD_RTLD_SUPPORTED) != 0)
        return -1;

    if ((mode & RTLD_LAZY) != 0 && (mode & RTLD_NOW) != 0)
        return -1;

    return 0;
}
