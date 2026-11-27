#include "internal/file_port.h"
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>

static int fail(long rc) {
    errno = (int) -rc;

    return -1;
}

static int finish(const astia_stat* value, struct stat* result) {
    if (value == NULL || result == NULL) {
        errno = EFAULT;

        return -1;
    }

    *result = (struct stat){0};

    result->st_dev     = value->dev;
    result->st_ino     = value->ino;
    result->st_mode    = (mode_t) value->mode;
    result->st_nlink   = value->nlink;
    result->st_uid     = value->uid;
    result->st_gid     = value->gid;
    result->st_rdev    = 0;
    result->st_size    = (off_t) value->size;
    result->st_blksize = 512;
    result->st_blocks  = (long) value->blocks;
    result->st_atime   = (time_t) value->atime_sec;
    result->st_mtime   = (time_t) value->mtime_sec;
    result->st_ctime   = (time_t) value->ctime_sec;

    return 0;
}

int stat(const char* path, struct stat* result) {
    if (path == NULL || result == NULL) {
        errno = EFAULT;

        return -1;
    }

    astia_stat value;

    long rc = __astia_fio_stat(path, &value, sizeof value);

    return rc < 0 ? fail(rc) : finish(&value, result);
}

int lstat(const char* path, struct stat* result) {
    if (path == NULL || result == NULL) {
        errno = EFAULT;

        return -1;
    }

    astia_stat value;

    long rc = __astia_fio_lstat(path, &value, sizeof value);

    return rc < 0 ? fail(rc) : finish(&value, result);
}

int fstat(int fd, struct stat* result) {
    if (result == NULL) {
        errno = EFAULT;

        return -1;
    }

    astia_stat value;

    long rc = __astia_fio_fstat(fd, &value, sizeof value);

    return rc < 0 ? fail(rc) : finish(&value, result);
}

int fstatat(int dirfd, const char* path, struct stat* result, int flags) {
    if (path == NULL || result == NULL) {
        errno = EFAULT;

        return -1;
    }

    astia_stat value;

    long rc = __astia_fio_statat(dirfd, path, &value, sizeof value, flags);

    return rc < 0 ? fail(rc) : finish(&value, result);
}

int chmod(const char* path, mode_t mode) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_chmod(path, (int) mode);

    return rc < 0 ? fail(rc) : 0;
}

int fchmod(int fd, mode_t mode) {
    if (fd < 0) {
        errno = EBADF;

        return -1;
    }

    long rc = __astia_fio_fchmod(fd, (int) mode);

    return rc < 0 ? fail(rc) : 0;
}

int chown(const char* path, uid_t owner, gid_t group) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_chown(path, owner, group, 0);

    return rc < 0 ? fail(rc) : 0;
}

int lchown(const char* path, uid_t owner, gid_t group) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_chown(path, owner, group, 1);

    return rc < 0 ? fail(rc) : 0;
}

int fchown(int fd, uid_t owner, gid_t group) {
    if (fd < 0) {
        errno = EBADF;

        return -1;
    }

    long rc = __astia_fio_fchown(fd, owner, group);

    return rc < 0 ? fail(rc) : 0;
}

int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
        errno = EINVAL;

        return -1;
    }

    struct astia_timespec        value[2];
    const struct astia_timespec* requested = NULL;

    if (times != NULL) {
        value[0].tv_sec  = (int64_t) times[0].tv_sec;
        value[0].tv_nsec = (int64_t) times[0].tv_nsec;
        value[1].tv_sec  = (int64_t) times[1].tv_sec;
        value[1].tv_nsec = (int64_t) times[1].tv_nsec;
        requested        = value;
    }

    long rc = __astia_fio_utimensat(dirfd, path, requested, flags);

    return rc < 0 ? fail(rc) : 0;
}

int utime(const char* path, const struct utimbuf* value) {
    if (value == NULL)
        return utimensat(AT_FDCWD, path, NULL, 0);

    struct timespec times[2] = {
        {value->actime, 0},
        {value->modtime, 0},
    };

    return utimensat(AT_FDCWD, path, times, 0);
}

int mkdir(const char* path, mode_t mode) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_mkdir(path, (int) mode);

    return rc < 0 ? fail(rc) : 0;
}

int mkdirat(int dirfd, const char* path, mode_t mode) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_mkdirat(dirfd, path, (int) mode);

    return rc < 0 ? fail(rc) : 0;
}

int mkfifo(const char* path, mode_t mode) {
    (void) path;
    (void) mode;

    errno = ENOSYS;

    return -1;
}

int unlink(const char* path) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_unlink(path, 0);

    return rc < 0 ? fail(rc) : 0;
}

int unlinkat(int dirfd, const char* path, int flags) {
    if (path == NULL || (flags & ~0x200) != 0) {
        errno = EINVAL;

        return -1;
    }

    long rc = __astia_fio_unlinkat(dirfd, path, (flags & 0x200) != 0);

    return rc < 0 ? fail(rc) : 0;
}

int rmdir(const char* path) {
    if (path == NULL) {
        errno = EFAULT;

        return -1;
    }

    long rc = __astia_fio_unlink(path, 1);

    return rc < 0 ? fail(rc) : 0;
}
