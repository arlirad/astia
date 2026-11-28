#pragma once

#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    const char* label;
    mode_t      mode;
    const char* expected;
} ls_mode_case;

static const ls_mode_case ls_mode_cases[] = {
    {"regular", S_IFREG | 0644, "-rw-r--r--"},
    {"directory", S_IFDIR | 0755, "drwxr-xr-x"},
    {"symlink", S_IFLNK | 0777, "lrwxrwxrwx"},
    {"character device", S_IFCHR | 0600, "crw-------"},
    {"block device", S_IFBLK | 0660, "brw-rw----"},
    {"fifo", S_IFIFO | 0644, "prw-r--r--"},
    {"socket", S_IFSOCK | 0755, "srwxr-xr-x"},
    {"unknown type", 0000000, "?---------"},
    {"unknown type with bits", 0160000, "?---------"},
    {"setuid exec", S_IFREG | 04755, "-rwsr-xr-x"},
    {"setuid no exec", S_IFREG | 04644, "-rwSr--r--"},
    {"setgid exec", S_IFREG | 02755, "-rwxr-sr-x"},
    {"setgid no exec", S_IFREG | 02644, "-rw-r-Sr--"},
    {"sticky exec", S_IFDIR | 01777, "drwxrwxrwt"},
    {"sticky no exec", S_IFDIR | 01766, "drwxrw-rwT"},
    {"setuid setgid exec", S_IFREG | 06755, "-rwsr-sr-x"},
    {"all special exec", S_IFREG | 07777, "-rwsrwsrwt"},
    {"all special no exec", S_IFREG | 07666, "-rwSrwSrwT"},
};
