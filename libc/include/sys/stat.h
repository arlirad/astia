#pragma once

#include <sys/types.h>

#include <time.h>
#include <utime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    mode_t        st_mode;
    unsigned long st_nlink;
    uid_t         st_uid;
    gid_t         st_gid;
    unsigned long st_rdev;
    off_t         st_size;
    long          st_blksize;
    long          st_blocks;
    time_t        st_atime;
    time_t        st_mtime;
    time_t        st_ctime;
};

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXU 0700
#define S_IRWXG 0070
#define S_IRWXO 0007
#define S_IXUGO 0111
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200

int stat(const char* path, struct stat* result);
int lstat(const char* path, struct stat* result);
int fstat(int fd, struct stat* result);
int fstatat(int dirfd, const char* path, struct stat* result, int flags);
int chmod(const char* path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char* path, uid_t owner, gid_t group);
int lchown(const char* path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags);
int mkdir(const char* path, mode_t mode);
int mkdirat(int dirfd, const char* path, mode_t mode);
int mkfifo(const char* path, mode_t mode);
int unlink(const char* path);
int rmdir(const char* path);

#ifdef __cplusplus
}
#endif
