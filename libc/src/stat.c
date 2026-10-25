#include "file_port.h"
#include <sys/stat.h>

#include <errno.h>

static int fail(long rc) { errno = (int)-rc; return -1; }

static int finish(const astia_stat* value, struct stat* result) {
    if (value == NULL || result == NULL) { errno = EFAULT; return -1; }
    result->st_dev = value->dev;
    result->st_ino = value->ino;
    result->st_size = (off_t)value->size;
    result->st_mode = (mode_t)(value->mode == 4 ? S_IFDIR :
                               value->mode == 2 ? S_IFCHR : S_IFREG);
    result->st_nlink = value->nlink;
    result->st_uid = value->uid;
    result->st_gid = value->gid;
    result->st_blocks = (long)value->blocks;
    result->st_atime = (time_t)value->atime_sec;
    result->st_mtime = (time_t)value->mtime_sec;
    result->st_ctime = (time_t)value->ctime_sec;
    return 0;
}

int stat(const char* path, struct stat* result) {
    if (path == NULL || result == NULL) { errno = EFAULT; return -1; }
    astia_stat value;
    long rc = __astia_fio_stat(path, &value, sizeof value);
    return rc < 0 ? fail(rc) : finish(&value, result);
}

int lstat(const char* path, struct stat* result) {
    return stat(path, result);
}

int fstat(int fd, struct stat* result) {
    if (result == NULL) { errno = EFAULT; return -1; }
    astia_stat value;
    long rc = __astia_fio_fstat(fd, &value, sizeof value);
    return rc < 0 ? fail(rc) : finish(&value, result);
}

int chmod(const char* path, mode_t mode) {
    (void) path;
    (void) mode;
    errno = ENOSYS; return -1;
}

int mkdir(const char* path, mode_t mode) {
    if (path == NULL) { errno = EFAULT; return -1; }
    long rc = __astia_fio_mkdir(path, (int)mode);
    return rc < 0 ? fail(rc) : 0;
}

int mkfifo(const char* path, mode_t mode) {
    (void) path;
    (void) mode;
    errno = ENOSYS; return -1;
}

int unlink(const char* path) {
    if (path == NULL) { errno = EFAULT; return -1; }
    long rc = __astia_fio_unlink(path, 0);
    return rc < 0 ? fail(rc) : 0;
}

int rmdir(const char* path) {
    if (path == NULL) { errno = EFAULT; return -1; }
    long rc = __astia_fio_unlink(path, 1);
    return rc < 0 ? fail(rc) : 0;
}
