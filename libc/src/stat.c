#include <errno.h>
#include <sys/stat.h>

static int unsupported(void) {
    errno = ENOSYS;
    return -1;
}

int stat(const char* path, struct stat* result) {
    (void) path;
    (void) result;
    return unsupported();
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
