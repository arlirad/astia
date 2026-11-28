#include "internal/path.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int           failures;
static char          fake_cwd[astia_path_buffer_capacity * 4 + 1] = "/bin";
static long          fake_error;
static unsigned long last_requests[64];
static unsigned int  request_count;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

long __astia_getcwd_call(char* buffer, unsigned long length) {
    if (request_count < sizeof last_requests / sizeof last_requests[0])
        last_requests[request_count] = length;

    ++request_count;

    if (fake_error != 0)
        return fake_error;

    const size_t needed = strlen(fake_cwd) + 1;
    if (needed > length)
        return -ERANGE;

    memcpy(buffer, fake_cwd, needed);

    return (long) needed;
}

static void reset(const char* cwd, long error) {
    snprintf(fake_cwd, sizeof fake_cwd, "%s", cwd);

    fake_error    = error;
    request_count = 0;
}

static void test_caller_buffer(void) {
    char buffer[16];

    reset("/bin", 0);

    errno = 0;

    CHECK(getcwd(buffer, sizeof buffer) == buffer);
    CHECK(strcmp(buffer, "/bin") == 0);
    CHECK(errno == 0);
    CHECK(request_count == 1 && last_requests[0] == sizeof buffer);
}

static void test_caller_buffer_too_small(void) {
    char buffer[2] = {'x', 'y'};

    reset("/bin", 0);

    errno = 0;

    CHECK(getcwd(buffer, sizeof buffer) == NULL);
    CHECK(errno == ERANGE);
    CHECK(buffer[0] == 'x' && buffer[1] == 'y');
    CHECK(request_count == 1);
}

static void test_caller_buffer_failure(void) {
    char buffer[16];

    reset("/bin", -EACCES);

    errno = 0;

    CHECK(getcwd(buffer, sizeof buffer) == NULL);
    CHECK(errno == EACCES);
    CHECK(request_count == 1);
}

static void test_allocate_default(void) {
    reset("/usr/local/bin", 0);

    errno = 0;

    char* path = getcwd(NULL, 0);

    CHECK(path != NULL);
    CHECK(path != NULL && strcmp(path, "/usr/local/bin") == 0);
    CHECK(errno == 0);
    CHECK(request_count == 1 && last_requests[0] == astia_path_buffer_capacity);

    free(path);
}

static void test_allocate_long_path(void) {
    char long_path[astia_path_buffer_capacity * 2 + 1];

    long_path[0] = '/';

    for (size_t i = 1; i < sizeof long_path - 1; ++i) {
        long_path[i] = (char) ('a' + i % 26);
    }

    long_path[sizeof long_path - 1] = '\0';

    reset(long_path, 0);

    char* path = getcwd(NULL, 0);

    CHECK(path != NULL);
    CHECK(path != NULL && strcmp(path, long_path) == 0);
    CHECK(request_count == 3);
    CHECK(last_requests[0] == astia_path_buffer_capacity);
    CHECK(last_requests[1] == astia_path_buffer_capacity * 2);
    CHECK(last_requests[2] == astia_path_buffer_capacity * 4);

    free(path);
}

static void test_allocate_small_length(void) {
    reset("/bin", 0);

    char* path = getcwd(NULL, 1);

    CHECK(path != NULL);
    CHECK(path != NULL && strcmp(path, "/bin") == 0);
    CHECK(request_count == 4);
    CHECK(last_requests[0] == 1);
    CHECK(last_requests[1] == 2);
    CHECK(last_requests[2] == 4);
    CHECK(last_requests[3] == 8);

    free(path);
}

static void test_allocate_failure(void) {
    reset("/bin", -ENOENT);

    errno = 0;

    CHECK(getcwd(NULL, 0) == NULL);
    CHECK(errno == ENOENT);
    CHECK(request_count == 1);
}

static void test_allocate_impossible(void) {
    reset("/bin", 0);

    errno = 0;

    CHECK(getcwd(NULL, 2 * 1024 * 1024) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(request_count == 0);
}

static void test_grow_until_enomem(void) {
    reset("/bin", -ERANGE);

    errno = 0;

    CHECK(getcwd(NULL, 0) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(request_count > 1);
}

int main(void) {
    test_caller_buffer();
    test_caller_buffer_too_small();
    test_caller_buffer_failure();
    test_allocate_default();
    test_allocate_long_path();
    test_allocate_small_length();
    test_allocate_failure();
    test_allocate_impossible();
    test_grow_until_enomem();

    if (failures != 0) {
        printf("%d failure(s)\n", failures);

        return 1;
    }

    printf("getcwd: all checks passed\n");

    return 0;
}
