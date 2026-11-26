#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUFFIX_LENGTH 6
#define ATTEMPTS 100

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static const char temp_alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static void make_suffix(char* suffix, unsigned long value) {
    for (int i = SUFFIX_LENGTH - 1; i >= 0; --i) {
        suffix[i] = temp_alphabet[value % (sizeof temp_alphabet - 1)];
        value /= sizeof temp_alphabet - 1;
    }

    suffix[SUFFIX_LENGTH] = '\0';
}

static void make_name(char* out, size_t size, const char* prefix, unsigned long value) {
    char suffix[SUFFIX_LENGTH + 1];

    make_suffix(suffix, value);
    snprintf(out, size, "%s%s", prefix, suffix);
}

static int file_exists(const char* path) {
    struct stat status;

    return lstat(path, &status) == 0;
}

static int create_file(const char* path) {
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0)
        return -1;

    return close(fd);
}

static void test_invalid_templates(void) {
    char short_name[] = "abc";
    char five[]       = "abcXXXXX";
    char middle[]     = "abcXXXXXXdef";

    errno = 0;

    CHECK(mktemp(NULL) == NULL);
    CHECK(errno == EINVAL);

    errno = 0;

    CHECK(mktemp(short_name) == NULL);
    CHECK(errno == EINVAL);

    errno = 0;

    CHECK(mktemp(five) == NULL);
    CHECK(errno == EINVAL);

    errno = 0;

    CHECK(mktemp(middle) == NULL);
    CHECK(errno == EINVAL);
}

static void test_success_and_no_reservation(void) {
    static const char prefix[] = "/tmp/astia-mktemp-success-";
    char              template_name[64];
    int               saved_errno;

    char* result;

    int fd;

    snprintf(template_name, sizeof template_name, "%sXXXXXX", prefix);

    errno       = EDOM;
    result      = mktemp(template_name);
    saved_errno = errno;

    CHECK(result == template_name);
    CHECK(saved_errno == EDOM);
    CHECK(strlen(template_name) == sizeof prefix - 1 + SUFFIX_LENGTH);
    CHECK(memcmp(template_name, prefix, sizeof prefix - 1) == 0);

    for (int i = 0; i < SUFFIX_LENGTH; ++i) {
        CHECK(strchr(temp_alphabet, template_name[sizeof prefix - 1 + i]) != NULL);
    }

    CHECK(!file_exists(template_name));
    CHECK(errno == ENOENT);

    fd = open(template_name, O_CREAT | O_EXCL | O_WRONLY, 0600);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(close(fd) == 0);
        CHECK(unlink(template_name) == 0);
    }
}

static void test_collision_skip(void) {
    static const char prefix[] = "/tmp/astia-mktemp-collision-";
    unsigned long     seed     = (unsigned long) getpid() ^ 2;
    char              template_name[64];
    char              occupied[64];
    char              expected[64];
    int               saved_errno;

    char* result;

    snprintf(template_name, sizeof template_name, "%sXXXXXX", prefix);
    make_name(occupied, sizeof occupied, prefix, seed);
    make_name(expected, sizeof expected, prefix, seed + 1);
    CHECK(create_file(occupied) == 0);

    errno       = ERANGE;
    result      = mktemp(template_name);
    saved_errno = errno;

    CHECK(result == template_name);
    CHECK(saved_errno == ERANGE);
    CHECK(strcmp(template_name, expected) == 0);
    CHECK(file_exists(occupied));
    CHECK(!file_exists(expected));
    unlink(occupied);
}

static void test_exhaustion(void) {
    static const char prefix[] = "/tmp/astia-mktemp-exhaust-";
    unsigned long     seed     = (unsigned long) getpid() ^ 3;
    char              template_name[64];
    char              candidate[64];
    char              last[64];

    char* result;

    snprintf(template_name, sizeof template_name, "%sXXXXXX", prefix);

    for (int i = 0; i < ATTEMPTS; ++i) {
        make_name(candidate, sizeof candidate, prefix, seed + (unsigned long) i);
        CHECK(create_file(candidate) == 0);
    }

    errno  = 0;
    result = mktemp(template_name);

    CHECK(result == NULL);
    CHECK(errno == EEXIST);
    make_name(last, sizeof last, prefix, seed + ATTEMPTS - 1);
    CHECK(strcmp(template_name, last) == 0);

    for (int i = 0; i < ATTEMPTS; ++i) {
        make_name(candidate, sizeof candidate, prefix, seed + (unsigned long) i);
        unlink(candidate);
    }
}

static void test_minimum_template(void) {
    char template_name[] = "XXXXXX";
    int  saved_errno;

    char* result;

    errno       = ECHILD;
    result      = mktemp(template_name);
    saved_errno = errno;

    CHECK(result == template_name);
    CHECK(saved_errno == ECHILD);
    CHECK(strlen(template_name) == SUFFIX_LENGTH);

    for (int i = 0; i < SUFFIX_LENGTH; ++i) {
        CHECK(strchr(temp_alphabet, template_name[i]) != NULL);
    }

    CHECK(!file_exists(template_name));
    CHECK(errno == ENOENT);
}

int main(void) {
    test_invalid_templates();
    test_success_and_no_reservation();
    test_collision_skip();
    test_exhaustion();
    test_minimum_template();

    return fails == 0 ? 0 : 1;
}
