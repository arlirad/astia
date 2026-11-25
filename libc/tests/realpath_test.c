#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

struct fixtures {
    char base[64];
    char canonical[64];
    char file[80];
    char sub[80];
    char link[80];
    char dangling[80];
    char loop_a[80];
    char loop_b[80];
};

static void make_path(char* out, size_t size, const char* base, const char* suffix) {
    snprintf(out, size, "%s/%s", base, suffix);
}

static void remove_fixtures(const struct fixtures* fixtures) {
    unlink(fixtures->link);
    unlink(fixtures->dangling);
    unlink(fixtures->loop_a);
    unlink(fixtures->loop_b);
    unlink(fixtures->file);
    rmdir(fixtures->sub);
    rmdir(fixtures->base);
}

static int create_fixtures(struct fixtures* fixtures) {
    snprintf(fixtures->base, sizeof fixtures->base, "/tmp/astia-realpath-%d", getpid());
    make_path(fixtures->file, sizeof fixtures->file, fixtures->base, "file");
    make_path(fixtures->sub, sizeof fixtures->sub, fixtures->base, "sub");
    make_path(fixtures->link, sizeof fixtures->link, fixtures->base, "link");
    make_path(fixtures->dangling, sizeof fixtures->dangling, fixtures->base, "dangling");
    make_path(fixtures->loop_a, sizeof fixtures->loop_a, fixtures->base, "loop_a");
    make_path(fixtures->loop_b, sizeof fixtures->loop_b, fixtures->base, "loop_b");

    remove_fixtures(fixtures);
    if (mkdir(fixtures->base, 0755) != 0)
        return -1;
    if (mkdir(fixtures->sub, 0755) != 0)
        return -1;

    int fd = open(fixtures->file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    if (write(fd, "realpath\n", 9) != 9) {
        close(fd);
        return -1;
    }
    if (close(fd) != 0)
        return -1;

    if (symlink("file", fixtures->link) != 0)
        return -1;
    if (symlink("missing", fixtures->dangling) != 0)
        return -1;
    if (symlink("loop_b", fixtures->loop_a) != 0)
        return -1;
    if (symlink("loop_a", fixtures->loop_b) != 0)
        return -1;
    if (realpath(fixtures->base, fixtures->canonical) == NULL)
        return -1;
    return 0;
}

static void test_absolute_and_dots(const struct fixtures* fixtures) {
    char buffer[128];
    char dotted[128];
    char expected[128];

    CHECK(realpath(fixtures->file, buffer) == buffer);
    CHECK(strcmp(buffer, fixtures->file) == 0);
    CHECK(buffer[0] == '/');

    snprintf(dotted, sizeof dotted, "%s/./sub/../file", fixtures->canonical);
    snprintf(expected, sizeof expected, "%s/file", fixtures->canonical);
    CHECK(realpath(dotted, buffer) == buffer);
    CHECK(strcmp(buffer, expected) == 0);
}

static void test_symlinks(const struct fixtures* fixtures) {
    char buffer[128];
    char expected[128];

    snprintf(expected, sizeof expected, "%s/file", fixtures->canonical);
    CHECK(realpath(fixtures->link, buffer) == buffer);
    CHECK(strcmp(buffer, expected) == 0);

    errno = 0;
    CHECK(realpath(fixtures->dangling, buffer) == NULL);
    CHECK(errno == ENOENT);

    errno = 0;
    CHECK(realpath(fixtures->loop_a, buffer) == NULL);
    CHECK(errno == ELOOP);
}

static void test_directory_traversal(const struct fixtures* fixtures) {
    char buffer[160];
    char path[160];
    char expected[160];

    snprintf(path, sizeof path, "%s/..", fixtures->file);
    errno = 0;
    CHECK(realpath(path, buffer) == NULL);
    CHECK(errno == ENOTDIR);

    snprintf(path, sizeof path, "%s/", fixtures->file);
    errno = 0;
    CHECK(realpath(path, buffer) == NULL);
    CHECK(errno == ENOTDIR);

    snprintf(path, sizeof path, "%s/..", fixtures->link);
    errno = 0;
    CHECK(realpath(path, buffer) == NULL);
    CHECK(errno == ENOTDIR);

    snprintf(path, sizeof path, "%s/", fixtures->sub);
    CHECK(realpath(path, buffer) == buffer);
    CHECK(strcmp(buffer, fixtures->sub) == 0);

    snprintf(path, sizeof path, "%s/..", fixtures->sub);
    snprintf(expected, sizeof expected, "%s", fixtures->canonical);
    CHECK(realpath(path, buffer) == buffer);
    CHECK(strcmp(buffer, expected) == 0);
}

static void test_invalid_arguments(char* buffer, size_t size) {
    errno = 0;
    CHECK(realpath(NULL, buffer) == NULL);
    CHECK(errno == EINVAL);

    char long_path[PATH_MAX + 1];
    memset(long_path, 'a', PATH_MAX);
    long_path[0]        = '/';
    long_path[PATH_MAX] = '\0';
    memset(buffer, 'L', size);
    errno = 0;
    CHECK(realpath(long_path, buffer) == NULL);
    CHECK(errno == ENAMETOOLONG);
    for (size_t i = 0; i < size; ++i)
        CHECK(buffer[i] == 'L');
}

static void test_allocation(const struct fixtures* fixtures) {
    char  expected[128];
    char* allocated;

    snprintf(expected, sizeof expected, "%s/file", fixtures->canonical);
    allocated = realpath(fixtures->link, NULL);
    CHECK(allocated != NULL);
    if (allocated != NULL) {
        CHECK(strcmp(allocated, expected) == 0);
        free(allocated);
    }
}

static void test_caller_buffer(const struct fixtures* fixtures) {
    char guard[128];

    memset(guard, 'Q', sizeof guard);
    CHECK(realpath(fixtures->file, guard + 1) == guard + 1);
    CHECK(guard[0] == 'Q');
    CHECK(guard[1 + strlen(fixtures->file) + 1] == 'Q');

    memset(guard, 'X', sizeof guard);
    errno = 0;
    CHECK(realpath(fixtures->dangling, guard) == NULL);
    CHECK(errno == ENOENT);
    for (size_t i = 0; i < sizeof guard; ++i)
        CHECK(guard[i] == 'X');
}

int main(void) {
    struct fixtures fixtures;
    char            scratch[80];

    if (create_fixtures(&fixtures) != 0) {
        remove_fixtures(&fixtures);
        return 2;
    }

    test_absolute_and_dots(&fixtures);
    test_symlinks(&fixtures);
    test_directory_traversal(&fixtures);
    test_invalid_arguments(scratch, sizeof scratch);
    test_allocation(&fixtures);
    test_caller_buffer(&fixtures);

    remove_fixtures(&fixtures);
    if (fails != 0) {
        printf("%d realpath checks failed\n", fails);
        return 1;
    }
    return 0;
}
