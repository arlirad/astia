#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/syscall.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int  fails;
extern long syscall(long number, ...);

static unsigned int host_uid(void) {
    return (unsigned int) syscall(SYS_getuid);
}

static unsigned int host_gid(void) {
    return (unsigned int) syscall(SYS_getgid);
}

static void poison_stat(struct stat* value) {
    memset(value, 0xA5, sizeof *value);
}

#define CHECK(x)                                                \
    do {                                                        \
        if (!(x)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++fails;                                            \
        }                                                       \
    } while (0)

int main(void) {
    const char* dir  = "astia_stat_test_dir";
    const char* file = "astia_stat_test_dir/file";
    const char* link = "astia_stat_test_dir/link";
    struct stat st, fst;

    int fd;

    CHECK(S_ISBLK(S_IFBLK) && !S_ISBLK(S_IFCHR));
    CHECK(S_ISSOCK(S_IFSOCK) && !S_ISSOCK(S_IFREG));

    (void) unlink(file);
    (void) unlink(link);
    (void) rmdir(dir);
    CHECK(mkdir(dir, 0755) == 0);
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK((st.st_mode & 0777) == 0755);
    CHECK(lstat(dir, &st) == 0 && S_ISDIR(st.st_mode));

    poison_stat(&st);
    CHECK(stat(dir, &st) == 0);
    CHECK(st.st_rdev == 0);
    CHECK(st.st_blksize == 512);

    fd = open(file, O_CREAT | O_RDWR, 0644);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(write(fd, "abc", 3) == 3);
        CHECK(fstat(fd, &fst) == 0 && S_ISREG(fst.st_mode) && fst.st_size == 3);
        CHECK(fst.st_atime != 0 && fst.st_mtime != 0 && fst.st_ctime != 0);
        poison_stat(&fst);
        CHECK(fstat(fd, &fst) == 0);
        CHECK(fst.st_rdev == 0);
        CHECK(fst.st_blksize == 512);
        CHECK(stat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3);
        CHECK(st.st_atime != 0 && st.st_mtime != 0 && st.st_ctime != 0);
        poison_stat(&st);
        CHECK(stat(file, &st) == 0);
        CHECK(st.st_rdev == 0);
        CHECK(st.st_blksize == 512);
        CHECK((st.st_mode & 0777) == 0644);
        CHECK(fchmod(fd, S_IFREG | 0620) == 0);
        CHECK(fstat(fd, &fst) == 0 && (fst.st_mode & 0777) == 0620);
        CHECK(chmod(file, 0600) == 0);
        CHECK(stat(file, &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(chown(file, host_uid(), host_gid()) == 0);
        CHECK(stat(file, &st) == 0 && st.st_uid == host_uid() && st.st_gid == host_gid());
        CHECK(close(fd) == 0);
    }

    fd = open(dir, O_RDONLY | O_DIRECTORY, 0);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(fchmod(fd, 0700) == 0);
        CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700);
        CHECK(close(fd) == 0);
    }

    fd = open(dir, O_RDONLY | O_DIRECTORY, 0);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(faccessat(fd, "file", R_OK, 0) == 0);
        CHECK(faccessat(fd, "file", R_OK, AT_EACCESS) == 0);

        errno = 0;

        CHECK(faccessat(fd, "missing", F_OK, 0) == -1 && errno == ENOENT);

        errno = 0;

        CHECK(faccessat(-1, "file", F_OK, 0) == -1 && errno == EBADF);

        errno = 0;

        CHECK(faccessat(fd, "file", F_OK, AT_EACCESS | 0x400) == -1 && errno == EINVAL);
        CHECK(close(fd) == 0);
    }

    errno = 0;

    CHECK(fchmod(-1, 0600) == -1 && errno == EBADF);
    CHECK(symlink("file", link) == 0);
    CHECK(lchown(link, host_uid(), host_gid()) == 0);
    CHECK(lstat(link, &st) == 0 && st.st_uid == host_uid() && st.st_gid == host_gid());

    fd = open(file, O_RDONLY, 0);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(fchown(fd, host_uid(), host_gid()) == 0);
        CHECK(close(fd) == 0);
    }

    CHECK(stat(link, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3);
    CHECK(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));

    poison_stat(&st);
    CHECK(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
    CHECK(st.st_rdev == 0);
    CHECK(st.st_blksize == 512);

    poison_stat(&st);
    CHECK(stat("/dev/null", &st) == 0 && S_ISCHR(st.st_mode));
    CHECK(st.st_rdev != 0xA5A5A5A5A5A5A5A5UL);
    CHECK(st.st_blksize == 512);

    {
        char target[16] = {0};

        CHECK(readlink(link, target, sizeof target) == 4 && strcmp(target, "file") == 0);
    }

    CHECK(symlink("missing", "astia_stat_test_dir/dangling") == 0);

    errno = 0;

    CHECK(stat("astia_stat_test_dir/dangling", &st) == -1 && errno == ENOENT);
    CHECK(lstat("astia_stat_test_dir/dangling", &st) == 0 && S_ISLNK(st.st_mode));

    errno = 0;

    CHECK(stat("astia_stat_test_dir/missing", &st) == -1 && errno == ENOENT);
    CHECK(stat(file, &st) == 0);

    time_t before = st.st_mtime;

    CHECK(utime(file, NULL) == 0 && stat(file, &st) == 0 && st.st_mtime >= before);

    errno = 0;

    CHECK(unlink(dir) == -1 && errno == EISDIR);
    CHECK(unlink(file) == 0);
    CHECK(unlink(link) == 0);
    CHECK(unlink("astia_stat_test_dir/dangling") == 0);
    CHECK(rmdir(dir) == 0);

    errno = 0;

    CHECK(rmdir(dir) == -1 && errno == ENOENT);

    return fails != 0;
}
