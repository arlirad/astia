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

struct observed_entry {
    char          name[16];
    unsigned long ino;
};

static int observe(DIR* stream, struct observed_entry* entries, int capacity) {
    struct dirent* entry;

    int count = 0;

    while ((entry = readdir(stream)) != NULL) {
        if (count < capacity) {
            unsigned int length = 0;

            while (entry->d_name[length] != '\0' && length + 1 < 16) {
                ++length;
            }

            memcpy(entries[count].name, entry->d_name, length);

            entries[count].name[length] = '\0';
            entries[count].ino          = entry->d_ino;
        }

        ++count;
    }

    return count;
}

static const struct observed_entry* find_observed(const struct observed_entry* entries,
                                                  int count, const char* name) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(entries[i].name, name) == 0)
            return &entries[i];
    }

    return NULL;
}

static void check_inode_identity(void) {
    struct observed_entry first[8];
    struct observed_entry second[8];

    char path[256];

    DIR* stream = opendir(directory);

    CHECK(stream != NULL);

    if (stream == NULL)
        return;

    int count = observe(stream, first, 8);

    CHECK(count == 3);

    for (int i = 0; i < count && i < 8; ++i) {
        struct stat st;

        CHECK(snprintf(path, sizeof path, "%s/%s", directory, first[i].name) > 0);
        CHECK(stat(path, &st) == 0);
        CHECK(first[i].ino == st.st_ino);

        for (int j = i + 1; j < count && j < 8; ++j) {
            CHECK(first[i].ino != first[j].ino);
        }
    }

    rewinddir(stream);

    int repeated = observe(stream, second, 8);

    CHECK(repeated == count);

    for (int i = 0; i < count && i < 8; ++i) {
        const struct observed_entry* match =
            find_observed(second, repeated, first[i].name);

        CHECK(match != NULL);
        CHECK(match != NULL && match->ino == first[i].ino);
    }

    CHECK(closedir(stream) == 0);
}

int main(void) {
    struct observed_entry observed[8];

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

    check_inode_identity();

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
        CHECK(observe(stream, observed, 8) == 3);
        CHECK(closedir(stream) == 0);
    }

    cleanup();

    if (fails != 0) {
        printf("%d failure(s)\n", fails);

        return 1;
    }

    return 0;
}
