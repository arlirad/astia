#include "file_port.h"
#include <sys/stat.h>

#include <errno.h>

static int unsupported(void) {
    errno = ENOSYS;
    return -1;
}

int stat(const char* path, struct stat* result) {
    astia_stat value;
    long rc = __astia_fio_stat(path, &value, sizeof value);
    if (rc < 0) { errno = (int)-rc; return -1; }
    result->st_dev = value.dev;
    result->st_ino = value.ino;
    result->st_size = (long)value.size;
    result->st_mode = (unsigned short)(value.mode == 4 ? S_IFDIR : S_IFREG);
    result->st_nlink = value.nlink;
    return 0;
}

int lstat(const char* path, struct stat* result) {
    (void) path;
    (void) result;
    return unsupported();
}

int fstat(int fd, struct stat* result) {
    (void) fd;
    (void) result;
    return unsupported();
}

int chmod(const char* path, mode_t mode) {
    (void) path;
    (void) mode;
    return unsupported();
}

int mkdir(const char* path, mode_t mode) {
    (void) path;
    (void) mode;
    return unsupported();
}

int mkfifo(const char* path, mode_t mode) {
    (void) path;
    (void) mode;
    return unsupported();
}

int unlink(const char* path) {
    (void) path;
    return unsupported();
}

int rmdir(const char* path) {
    (void) path;
    return unsupported();
}
