#include <astia/api/syscalls.h>

#include "internal/path.h"

__attribute__((weak)) long __astia_getcwd_call(char* buffer, unsigned long length) {
    return astia_getcwd(buffer, length);
}

__attribute__((weak)) long __astia_chdir_call(const char* path) {
    return astia_chdir(path);
}
