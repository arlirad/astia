#include "internal/path.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int  failures;
static int  chdir_calls;
static char chdir_path[astia_path_buffer_capacity];
static char fake_cwd[64] = "/bin";

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

long __astia_getcwd_call(char* buffer, unsigned long length) {
    const size_t needed = strlen(fake_cwd) + 1;
    if (needed > length)
        return -ERANGE;

    memcpy(buffer, fake_cwd, needed);

    return (long) needed;
}

long __astia_chdir_call(const char* path) {
    ++chdir_calls;

    snprintf(chdir_path, sizeof chdir_path, "%s", path);

    return 0;
}

static void reset(const char* cwd) {
    snprintf(fake_cwd, sizeof fake_cwd, "%s", cwd);

    chdir_calls   = 0;
    chdir_path[0] = '\0';
}

static void test_empty_path_does_not_change_the_cwd(void) {
    char cwd[64];

    reset("/bin");

    errno = 0;

    CHECK(chdir("") == -1);
    CHECK(errno == ENOENT);
    CHECK(chdir_calls == 0);

    CHECK(getcwd(cwd, sizeof cwd) == cwd);
    CHECK(strcmp(cwd, "/bin") == 0);
}

static void test_dot_resolves_to_the_cwd(void) {
    reset("/bin");

    CHECK(chdir(".") == 0);
    CHECK(chdir_calls == 1);
    CHECK(strcmp(chdir_path, "/bin") == 0);
}

static void test_relative_path_is_composed_from_the_cwd(void) {
    reset("/usr/local");

    CHECK(chdir("bin") == 0);
    CHECK(chdir_calls == 1);
    CHECK(strcmp(chdir_path, "/usr/local/bin") == 0);
}

int main(void) {
    test_empty_path_does_not_change_the_cwd();
    test_dot_resolves_to_the_cwd();
    test_relative_path_is_composed_from_the_cwd();

    if (failures != 0) {
        printf("%d failure(s)\n", failures);

        return 1;
    }

    printf("chdir: all checks passed\n");

    return 0;
}
