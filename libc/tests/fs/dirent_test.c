#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/syscall.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;

extern long syscall(long number, ...);

#define CHECK(x)                                                \
    do {                                                        \
        if (!(x)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++fails;                                            \
        }                                                       \
    } while (0)

static const char* const directory = "astia_dirent_test_dir";
static const char* const renamed   = "astia_dirent_test_renamed";

static void cleanup(void) {
    (void) chmod(directory, 0755);
    (void) unlink("astia_dirent_test_dir/alpha");
    (void) unlink("astia_dirent_test_dir/beta");
    (void) rmdir("astia_dirent_test_dir/nested");
    (void) rmdir(directory);
    (void) rmdir(renamed);
}

static int collect(DIR* stream, char names[][16], int capacity) {
    struct dirent* entry;

    int count = 0;

    while ((entry = readdir(stream)) != NULL) {
        if (count < capacity) {
            unsigned int length = 0;

            while (entry->d_name[length] != '\0' && length + 1 < 16) {
                ++length;
            }

            memcpy(names[count], entry->d_name, length);

            names[count][length] = '\0';
        }

        ++count;
    }

    return count;
}

int main(void) {
    char names[8][16];

    DIR* stream;

    int fd;

    CHECK(DT_UNKNOWN == 0 && DT_FIFO == 1 && DT_CHR == 2 && DT_DIR == 4);
    CHECK(DT_BLK == 6 && DT_REG == 8 && DT_LNK == 10 && DT_SOCK == 12);

    cleanup();

    CHECK(mkdir(directory, 0755) == 0);
    CHECK(mkdir("astia_dirent_test_dir/nested", 0755) == 0);

    fd = open("astia_dirent_test_dir/alpha", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    CHECK(fd >= 0);

    if (fd >= 0)
        CHECK(close(fd) == 0);

    fd = open("astia_dirent_test_dir/beta", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    CHECK(fd >= 0);

    if (fd >= 0)
        CHECK(close(fd) == 0);

    errno = 0;

    stream = opendir(directory);

    CHECK(stream != NULL);

    if (stream != NULL) {
        int alpha  = 0;
        int beta   = 0;
        int nested = 0;
        int count  = 0;

        struct dirent* entry;

        while ((entry = readdir(stream)) != NULL) {
            ++count;

            if (strcmp(entry->d_name, "alpha") == 0) {
                ++alpha;
                CHECK(entry->d_type == DT_REG);
            }
            else if (strcmp(entry->d_name, "beta") == 0) {
                ++beta;
                CHECK(entry->d_type == DT_REG);
            }
            else if (strcmp(entry->d_name, "nested") == 0) {
                ++nested;
                CHECK(entry->d_type == DT_DIR);
            }
        }

        CHECK(count == 3);
        CHECK(alpha == 1 && beta == 1 && nested == 1);

        errno = 0;

        CHECK(readdir(stream) == NULL && errno == 0);

        rewinddir(stream);
        CHECK(readdir(stream) != NULL);

        CHECK(closedir(stream) == 0);
    }

    errno = 0;

    CHECK(opendir(NULL) == NULL && errno == EINVAL);

    errno = 0;

    CHECK(opendir("astia_dirent_test_missing") == NULL && errno == ENOENT);

    errno = 0;

    CHECK(opendir("astia_dirent_test_dir/alpha") == NULL && errno == ENOTDIR);

    if ((unsigned int) syscall(SYS_geteuid) != 0) {
        CHECK(chmod(directory, 0) == 0);

        errno = 0;

        CHECK(opendir(directory) == NULL && errno == EACCES);

        CHECK(chmod(directory, 0755) == 0);
    }

    stream = opendir(directory);
    if (stream != NULL) {
        CHECK(renameat(AT_FDCWD, directory, AT_FDCWD, renamed) == 0);
        CHECK(readdir(stream) != NULL);
        CHECK(closedir(stream) == 0);
        CHECK(renameat(AT_FDCWD, renamed, AT_FDCWD, directory) == 0);
    }

    stream = opendir(directory);
    if (stream != NULL) {
        CHECK(collect(stream, names, 8) == 3);
        CHECK(closedir(stream) == 0);
    }

    cleanup();

    if (fails != 0) {
        printf("%d failure(s)\n", fails);

        return 1;
    }

    return 0;
}
