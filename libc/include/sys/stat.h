#pragma once

#include <sys/types.h>

#include <time.h>

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
#define S_IXUGO 0111

int stat(const char* path, struct stat* result);
int lstat(const char* path, struct stat* result);
int fstat(int fd, struct stat* result);
int chmod(const char* path, mode_t mode);
int mkdir(const char* path, mode_t mode);
int mkfifo(const char* path, mode_t mode);
int unlink(const char* path);
int rmdir(const char* path);

#ifdef __cplusplus
}
#endif
