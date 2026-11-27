/* Host-only fault injection for close() failures.
 *
 * Each tool main is linked into this binary under a renamed symbol and
 * wrapped close() fails the configured close call.  This covers the case a
 * delayed-write or remote backend reports data loss only at close(), which
 * the in-guest tmpfs cannot simulate. */

#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_CAP 1024

typedef int (*tool_main)(int argc, char** argv);

typedef struct {
    const char* name;
    tool_main   entry;
    char**      argv;
    int         argc;
    const char* input;
    int         fail_ordinal;
    int         expected_status;
    const char* expected_output;
    const char* expected_destination;
    int         expected_close_calls;
} close_case;

int cu_head_main(int argc, char** argv);
int cu_wc_main(int argc, char** argv);
int cu_grep_main(int argc, char** argv);
int cu_sort_main(int argc, char** argv);
int cu_cp_main(int argc, char** argv);
int cu_mv_main(int argc, char** argv);

int __real_close(int fd);

static int close_fail_ordinal = -1;
static int close_calls;

int __wrap_close(int fd) {
    ++close_calls;

    if (close_fail_ordinal == close_calls) {
        errno = EIO;

        return -1;
    }

    return __real_close(fd);
}

static char input_path[TEST_PATH_CAP];
static char destination_path[TEST_PATH_CAP];
static char output_path[TEST_PATH_CAP];
static char error_path[TEST_PATH_CAP];
static char count_path[TEST_PATH_CAP];

static int fail(const char* what) {
    fprintf(stderr, "close error test failed: %s\n", what);

    return 1;
}

static int write_text(const char* path, const char* text) {
    size_t length = strlen(text);
    size_t offset = 0;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;

    while (offset < length) {
        ssize_t written = write(fd, text + offset, length - offset);

        if (written < 0 && errno == EINTR)
            continue;

        if (written <= 0) {
            close(fd);

            return -1;
        }

        offset += (size_t) written;
    }

    return close(fd);
}

static int file_equals(const char* path, const char* expected) {
    char buffer[4096];

    FILE* file = fopen(path, "r");
    if (file == NULL)
        return 0;

    size_t length = strlen(expected);
    size_t got    = fread(buffer, 1, sizeof buffer - 1, file);

    int equal =
        got == length && memcmp(buffer, expected, length) == 0 && fgetc(file) == EOF;

    fclose(file);

    return equal;
}

static int file_contains(const char* path, const char* needle) {
    char buffer[4096];

    FILE* file = fopen(path, "r");
    if (file == NULL)
        return 0;

    size_t got = fread(buffer, 1, sizeof buffer - 1, file);

    buffer[got] = '\0';

    fclose(file);

    return strstr(buffer, needle) != NULL;
}

static int read_close_calls(int* calls) {
    FILE* file = fopen(count_path, "r");
    if (file == NULL)
        return -1;

    int value = 0;
    int ok    = fscanf(file, "%d", &value) == 1;

    fclose(file);

    if (!ok)
        return -1;

    *calls = value;

    return 0;
}

static int run_case(const close_case* test) {
    if (test->input != NULL && write_text(input_path, test->input) != 0)
        return fail("write input");

    pid_t child = fork();
    if (child < 0)
        return fail("fork");

    if (child == 0) {
        int output = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int error  = open(error_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (output < 0 || error < 0 || dup2(output, 1) < 0 || dup2(error, 2) < 0)
            _Exit(200);

        if (output > 2)
            close(output);

        if (error > 2)
            close(error);

        close_calls        = 0;
        close_fail_ordinal = test->fail_ordinal;

        int status = test->entry(test->argc, test->argv);
        int calls  = close_calls;

        close_fail_ordinal = -1;

        int count_fd = open(count_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (count_fd >= 0) {
            char text[32];
            int  length = snprintf(text, sizeof text, "%d\n", calls);

            (void) write(count_fd, text, (size_t) length);
            close(count_fd);
        }

        _Exit(status & 255);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
        return fail("waitpid");

    if (WEXITSTATUS(status) != test->expected_status) {
        printf("%s: status %d, expected %d\n", test->name, WEXITSTATUS(status),
               test->expected_status);

        return 1;
    }

    if (!file_contains(error_path, "close failed")) {
        printf("%s: missing close error report\n", test->name);

        return 1;
    }

    if (test->expected_output != NULL &&
        !file_equals(output_path, test->expected_output)) {
        printf("%s: unexpected output\n", test->name);

        return 1;
    }

    if (test->expected_destination != NULL &&
        !file_equals(destination_path, test->expected_destination)) {
        printf("%s: destination content changed\n", test->name);

        return 1;
    }

    int calls = 0;

    if (read_close_calls(&calls) != 0 || calls != test->expected_close_calls) {
        printf("%s: expected %d close calls, got %d\n", test->name,
               test->expected_close_calls, calls);

        return 1;
    }

    return 0;
}

static int cross_device_checks(const char* root, const char* text) {
    char shm_root[] = "/dev/shm/astia-close-XXXXXX";

    if (mkdtemp(shm_root) == NULL) {
        printf("close error test: mv cross-device checks skipped (no /dev/shm)\n");

        return 0;
    }

    struct stat root_info;
    struct stat shm_info;

    if (stat(root, &root_info) != 0 || stat(shm_root, &shm_info) != 0 ||
        root_info.st_dev == shm_info.st_dev) {
        printf("close error test: mv cross-device checks skipped (single filesystem)\n");
        rmdir(shm_root);

        return 0;
    }

    char shm_destination[TEST_PATH_CAP];

    snprintf(shm_destination, sizeof shm_destination, "%s/result", shm_root);

    char* mv_input_close[]  = {"mv", input_path, shm_destination, NULL};
    char* mv_output_close[] = {"mv", input_path, shm_destination, NULL};

    close_case cases[] = {
        {"mv input close", cu_mv_main, mv_input_close, 3, text, 1, 1, NULL, NULL, 2},
        {"mv output close", cu_mv_main, mv_output_close, 3, text, 2, 1, NULL, NULL, 2},
    };

    int status = 0;

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (run_case(&cases[i]) != 0) {
            status = 1;

            break;
        }

        if (file_equals(shm_destination, text) == 0) {
            printf("%s: destination content changed\n", cases[i].name);

            status = 1;

            break;
        }

        unlink(input_path);
    }

    unlink(shm_destination);
    rmdir(shm_root);

    return status;
}

int main(void) {
    char root[] = "/tmp/astia-close-XXXXXX";

    if (mkdtemp(root) == NULL)
        return fail("mkdtemp");

    snprintf(input_path, sizeof input_path, "%s/input", root);
    snprintf(destination_path, sizeof destination_path, "%s/destination", root);
    snprintf(output_path, sizeof output_path, "%s/stdout", root);
    snprintf(error_path, sizeof error_path, "%s/stderr", root);
    snprintf(count_path, sizeof count_path, "%s/count", root);

    const char* text        = "one\ntwo\nthree\n";
    const char* sort_input  = "two\nthree\none\n";
    const char* sort_output = "one\nthree\ntwo\n";

    char wc_expected[TEST_PATH_CAP + 16];

    snprintf(wc_expected, sizeof wc_expected, "3 %s\n", input_path);

    char* head_argv[]       = {"head", input_path, NULL};
    char* wc_argv[]         = {"wc", "-l", input_path, NULL};
    char* grep_argv[]       = {"grep", "two", input_path, NULL};
    char* sort_argv[]       = {"sort", input_path, NULL};
    char* cp_input_close[]  = {"cp", input_path, destination_path, NULL};
    char* cp_output_close[] = {"cp", input_path, destination_path, NULL};

    close_case cases[] = {
        {"head input close", cu_head_main, head_argv, 2, text, 1, 1, text, NULL, 1},
        {"wc input close", cu_wc_main, wc_argv, 3, text, 1, 1, wc_expected, NULL, 1},
        {"grep input close", cu_grep_main, grep_argv, 3, text, 1, 2, "two\n", NULL, 1},
        {"sort input close", cu_sort_main, sort_argv, 2, sort_input, 1, 1, sort_output,
         NULL, 1},
        {"cp input close", cu_cp_main, cp_input_close, 3, text, 1, 1, NULL, text, 2},
        {"cp output close", cu_cp_main, cp_output_close, 3, text, 2, 1, NULL, text, 2},
    };

    int status = 0;

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (run_case(&cases[i]) != 0) {
            status = 1;

            break;
        }
    }

    if (status == 0)
        status = cross_device_checks(root, text);

    unlink(input_path);
    unlink(destination_path);
    unlink(output_path);
    unlink(error_path);
    unlink(count_path);
    rmdir(root);

    if (status == 0)
        printf("close error tests ok\n");

    return status;
}
