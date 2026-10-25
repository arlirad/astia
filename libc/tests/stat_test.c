#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++fails; } } while (0)

int main(void) {
    const char* dir = "astia_stat_test_dir";
    const char* file = "astia_stat_test_dir/file";
    struct stat st, fst;
    int fd;

    (void)unlink(file);
    (void)rmdir(dir);
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
        CHECK(close(fd) == 0);
    }
    errno = 0;
    CHECK(stat("astia_stat_test_dir/missing", &st) == -1 && errno == ENOENT);
    errno = 0;
    CHECK(unlink(dir) == -1 && errno == EISDIR);
    CHECK(unlink(file) == 0);
    CHECK(rmdir(dir) == 0);
    errno = 0;
    CHECK(rmdir(dir) == -1 && errno == ENOENT);
    return fails != 0;
}
