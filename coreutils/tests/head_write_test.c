#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CAPTURE_LIMIT (64 * 1024)

static char   capture[CAPTURE_LIMIT];
static size_t capture_length;
static size_t write_calls;
static int    fail_writes;

int head_main(int argc, char** argv);

ssize_t head_test_write(int fd, const void* data, size_t length) {
    (void) fd;

    ++write_calls;

    if (fail_writes) {
        errno = EIO;

        return -1;
    }

    if (length > CAPTURE_LIMIT - capture_length)
        length = CAPTURE_LIMIT - capture_length;

    memcpy(capture + capture_length, data, length);

    capture_length += length;

    return (ssize_t) length;
}

static int write_fixture(const char* path, const char* data, size_t length) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return 1;

    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written <= 0) {
            close(fd);

            return 1;
        }

        offset += (size_t) written;
    }

    return close(fd) == 0 ? 0 : 1;
}

static int report(const char* name, int status, size_t expected_calls) {
    printf("head write: %s status=%d writes=%zu expected_writes=%zu bytes=%zu\n", name,
           status, write_calls, expected_calls, capture_length);

    return 1;
}

static int report_capture(const char* name, int status, const char* expected) {
    if (status != 0 || capture_length != strlen(expected) ||
        memcmp(capture, expected, capture_length) != 0) {
        printf("head write: %s status=%d writes=%zu bytes=%zu\n", name, status,
               write_calls, capture_length);

        return 1;
    }

    return 0;
}

static void reset_capture(int fail_output) {
    capture_length = 0;
    write_calls    = 0;
    fail_writes    = fail_output;
}

static int prepare_pair(const char* first_path, const char* second_path) {
    if (write_fixture(first_path, "a\n", 2) != 0 ||
        write_fixture(second_path, "b\n", 2) != 0)
        return 1;

    reset_capture(0);

    return 0;
}

static int run_header_cases(const char* path, const char* second_path) {
    char expected[512];

    char* multi[] = {"head", (char*) path, (char*) second_path};

    if (prepare_pair(path, second_path) != 0)
        return 1;

    snprintf(expected, sizeof expected, "==> %s <==\na\n\n==> %s <==\nb\n", path,
             second_path);

    if (report_capture("multiple files", head_main(3, multi), expected) != 0)
        return 1;

    char* quiet[] = {"head", "-q", (char*) path, (char*) second_path};

    if (prepare_pair(path, second_path) != 0)
        return 1;

    if (report_capture("quiet", head_main(4, quiet), "a\nb\n") != 0)
        return 1;

    char* last_option[] = {"head", "-q", "-v", (char*) path, (char*) second_path};

    if (prepare_pair(path, second_path) != 0)
        return 1;

    snprintf(expected, sizeof expected, "==> %s <==\na\n\n==> %s <==\nb\n", path,
             second_path);

    if (report_capture("last option wins", head_main(5, last_option), expected) != 0)
        return 1;

    char* verbose[] = {"head", "-v", (char*) path};

    if (prepare_pair(path, second_path) != 0)
        return 1;

    snprintf(expected, sizeof expected, "==> %s <==\na\n", path);

    if (report_capture("verbose", head_main(3, verbose), expected) != 0)
        return 1;

    char* zero[] = {"head", "-n", "0", (char*) path, (char*) second_path};

    if (prepare_pair(path, second_path) != 0)
        return 1;

    snprintf(expected, sizeof expected, "==> %s <==\n\n==> %s <==\n", path, second_path);

    return report_capture("zero lines", head_main(5, zero), expected);
}

static int run_case(const char* name, const char* path, const char* wanted,
                    const char* data, size_t length, const char* expected,
                    size_t expected_calls, int fail_output) {
    reset_capture(fail_output);

    if (write_fixture(path, data, length) != 0) {
        printf("head write: fixture %s failed\n", name);

        return 1;
    }

    char* argv[] = {"head", "-n", (char*) wanted, (char*) path};

    int status = head_main(4, argv);

    (void) unlink(path);

    if (fail_output)
        return status == 1 ? 0 : report(name, status, expected_calls);

    if (status != 0 || write_calls != expected_calls)
        return report(name, status, expected_calls);

    if (capture_length != strlen(expected) ||
        memcmp(capture, expected, capture_length) != 0)
        return report(name, status, expected_calls);

    return 0;
}

int main(void) {
    static char long_input[4096 + 8];
    static char long_expected[4096 + 1];

    char path[] = "/tmp/head_write_test_XXXXXX";

    int fd = mkstemp(path);
    if (fd < 0) {
        printf("head write: mkstemp failed\n");

        return 1;
    }

    close(fd);

    char second_path[] = "/tmp/head_write_test_XXXXXX";

    fd = mkstemp(second_path);
    if (fd < 0) {
        printf("head write: second mkstemp failed\n");

        return 1;
    }

    close(fd);

    memset(long_input, 'a', 4096);
    memcpy(long_input + 4096, "\nB\nC\n", 6);

    memset(long_expected, 'a', 4096);

    long_expected[4096] = '\n';

    if (run_case("one chunk", path, "2", "one\ntwo\nthree\n", 14, "one\ntwo\n", 1, 0) !=
            0 ||
        run_case("line across read buffer", path, "1", long_input, 4101, long_expected, 2,
                 0) != 0 ||
        run_case("write failure", path, "10", "a\nb\n", 4, NULL, 0, 1) != 0 ||
        run_header_cases(path, second_path) != 0)
        return 1;

    (void) unlink(path);
    (void) unlink(second_path);

    printf("head write tests ok\n");

    return 0;
}
