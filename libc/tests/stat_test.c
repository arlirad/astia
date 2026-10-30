#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/syscall.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int          fails;
extern long         syscall(long number, ...);
static unsigned int host_uid(void) {
    return (unsigned int) syscall(SYS_getuid);
}
static unsigned int host_gid(void) {
    return (unsigned int) syscall(SYS_getgid);
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
    int         fd;

    (void) unlink(file);
    (void) unlink(link);
    (void) rmdir(dir);
    CHECK(mkdir(dir, 0755) == 0);
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK((st.st_mode & 0777) == 0755);
    CHECK(lstat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    fd = open(file, O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, "abc", 3) == 3);
        CHECK(fstat(fd, &fst) == 0 && S_ISREG(fst.st_mode) && fst.st_size == 3);
        CHECK(stat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3);
        CHECK((st.st_mode & 0777) == 0644);
        CHECK(chmod(file, 0600) == 0);
        CHECK(stat(file, &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(chown(file, host_uid(), host_gid()) == 0);
        CHECK(stat(file, &st) == 0 && st.st_uid == host_uid() && st.st_gid == host_gid());
        CHECK(close(fd) == 0);
    }
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
