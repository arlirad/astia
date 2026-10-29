#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void) {
    const char* source = "/tmp/astia_rename_test_source";
    const char* target = "/tmp/astia_rename_test_target";
    int fd;
    struct stat a, b;
    char buffer[16] = {0};

    (void)unlink(source);
    (void)unlink(target);
    fd = open(source, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0 || write(fd, "source", 6) != 6 || close(fd) != 0) return 1;
    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0 || write(fd, "target", 6) != 6 || close(fd) != 0) return 1;

    if (rename(source, target) != 0 || access(source, F_OK) == 0) return 1;
    fd = open(target, O_RDONLY);
    if (fd < 0 || read(fd, buffer, sizeof buffer) != 6 || close(fd) != 0 ||
        memcmp(buffer, "source", 6) != 0)
        return 1;
    errno = 0;
    if (rename("/tmp/astia_rename_test_missing", target) == 0 || errno != ENOENT) return 1;
    if (link(target, source) != 0) return 1;
    if (stat(target, &a) != 0 || stat(source, &b) != 0 || a.st_ino != b.st_ino ||
        a.st_nlink != 2) return 1;
    if (unlink(source) != 0 || stat(target, &a) != 0 || a.st_nlink != 1) return 1;
    (void)unlink(target);
    return 0;
}
