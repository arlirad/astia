#include <sys/types.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define read login_test_read
ssize_t login_test_read(int fd, void* buffer, size_t count);
#define main login_program_main
#include "../src/login.c"
#undef main
#undef read

static const char* input;
static size_t      input_offset;
static int         interrupt_pending;

ssize_t login_test_read(int fd, void* buffer, size_t count) {
    (void) fd;
    (void) count;

    if (interrupt_pending) {
        interrupt_pending = 0;
        errno             = EINTR;

        return -1;
    }

    if (input[input_offset] == '\0')
        return 0;

    ((char*) buffer)[0] = input[input_offset++];

    return 1;
}

static int check_name(const char* label, const char* script, int interrupt,
                      unsigned long size, int expected_status,
                      const char* expected_name) {
    char name[64];

    input             = script;
    input_offset      = 0;
    interrupt_pending = interrupt;

    int status = read_name(name, size);

    if (status != expected_status ||
        (expected_name != NULL && strcmp(name, expected_name) != 0)) {
        printf("login read test failed: %s (status=%d name=[%s])\n", label, status,
               status == 0 ? name : "");

        return 1;
    }

    return 0;
}

int main(void) {
    if (check_name("interrupted then split", "root\n", 1, 64, 0, "root") ||
        check_name("partial reads", "alice\r", 0, 64, 0, "alice") ||
        check_name("eof without newline", "bob", 0, 64, 0, "bob") ||
        check_name("empty input", "", 0, 64, -1, NULL) ||
        check_name("empty line", "\n", 0, 64, -1, NULL) ||
        check_name("colon rejected", "a:b\n", 0, 64, -1, NULL) ||
        check_name("bounded buffer", "abcdefghijklmnop\n", 0, 8, -1, NULL))
        return 1;

    printf("login read test ok\n");

    return 0;
}
