/*
 * execvp() PATH search against a real host kernel: an EACCES candidate
 * is remembered and the search continues, so a later executable still
 * runs; when every candidate is denied the saved EACCES is returned.
 * The host port supplies the raw execve/fork syscalls; the search
 * loop under test is ours.
 */
#include <sys/stat.h>
#include <sys/wait.h>

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

enum { copy_buffer_size = 4096 };

static int write_all(int fd, const char* data, size_t size) {
    while (size > 0) {
        ssize_t written = write(fd, data, size);
        if (written <= 0)
            return -1;

        data += written;
        size -= (size_t) written;
    }

    return 0;
}

static int create_plain_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;

    int result = write_all(fd, "not executable\n", 15);

    if (close(fd) != 0)
        result = -1;

    if (result != 0)
        return -1;

    return chmod(path, 0644);
}

static int copy_executable(const char* source, const char* target) {
    char buffer[copy_buffer_size];

    int in = open(source, O_RDONLY);

    if (in < 0)
        return -1;

    int out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) {
        close(in);

        return -1;
    }

    int result = 0;

    for (;;) {
        ssize_t got = read(in, buffer, sizeof buffer);
        if (got < 0) {
            result = -1;

            break;
        }

        if (got == 0)
            break;

        if (write_all(out, buffer, (size_t) got) != 0) {
            result = -1;

            break;
        }
    }

    if (close(in) != 0)
        result = -1;

    if (close(out) != 0)
        result = -1;

    if (result != 0)
        return -1;

    return chmod(target, 0755);
}

static int current_executable(char* out, size_t capacity, const char* argv0) {
    long length = readlink("/proc/self/exe", out, capacity - 1);

    if (length > 0 && (size_t) length < capacity) {
        out[length] = '\0';

        return 0;
    }

    return realpath(argv0, out) == NULL ? -1 : 0;
}

static int executable_directory(char* out, size_t capacity, const char* argv0) {
    char self[PATH_MAX];

    if (current_executable(self, sizeof self, argv0) != 0)
        return -1;

    char* slash = strrchr(self, '/');
    if (slash == NULL)
        return -1;

    size_t length = (size_t) (slash - self);

    if (length + 1 > capacity)
        return -1;

    memcpy(out, self, length);

    out[length] = '\0';

    return 0;
}

static int wait_for_zero(int child) {
    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return -1;

    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--execvp-child") == 0)
        _Exit(0);

    char base[PATH_MAX];
    char self[PATH_MAX];
    char noexec[PATH_MAX];
    char bindir[PATH_MAX];
    char other[PATH_MAX];
    char noexec_tool[PATH_MAX];
    char bin_tool[PATH_MAX];
    char other_tool[PATH_MAX];
    char search_path[PATH_MAX];
    char denied_path[PATH_MAX];

    const int pid = (int) getpid();

    CHECK(executable_directory(base, sizeof base, argv[0]) == 0);
    CHECK(current_executable(self, sizeof self, argv[0]) == 0);

    snprintf(noexec, sizeof noexec, "%s/astia_execvp_noexec_%d", base, pid);
    snprintf(bindir, sizeof bindir, "%s/astia_execvp_bin_%d", base, pid);
    snprintf(other, sizeof other, "%s/astia_execvp_other_%d", base, pid);
    snprintf(noexec_tool, sizeof noexec_tool, "%s/tool", noexec);
    snprintf(bin_tool, sizeof bin_tool, "%s/tool", bindir);
    snprintf(other_tool, sizeof other_tool, "%s/tool", other);
    snprintf(search_path, sizeof search_path, "%s:%s", noexec, bindir);
    snprintf(denied_path, sizeof denied_path, "%s:%s", noexec, other);

    (void) unlink(noexec_tool);
    (void) unlink(bin_tool);
    (void) unlink(other_tool);
    (void) rmdir(noexec);
    (void) rmdir(bindir);
    (void) rmdir(other);

    CHECK(mkdir(noexec, 0755) == 0);
    CHECK(mkdir(bindir, 0755) == 0);
    CHECK(mkdir(other, 0755) == 0);
    CHECK(create_plain_file(noexec_tool) == 0);
    CHECK(create_plain_file(other_tool) == 0);
    CHECK(copy_executable(self, bin_tool) == 0);

    /* The first PATH entry denies execution; the second must still run. */
    int child = fork();

    CHECK(child >= 0);

    if (child == 0) {
        char* child_argv[] = {(char*) "tool", (char*) "--execvp-child", NULL};

        if (setenv("PATH", search_path, 1) != 0)
            _Exit(10);

        execvp("tool", child_argv);
        _Exit(errno == EACCES ? 11 : 12);
    }

    if (child > 0)
        CHECK(wait_for_zero(child) == 0);

    /* Every candidate is denied: the error must be EACCES, not ENOENT. */
    child = fork();

    CHECK(child >= 0);

    if (child == 0) {
        char* child_argv[] = {(char*) "tool", NULL};

        if (setenv("PATH", denied_path, 1) != 0)
            _Exit(20);

        errno = 0;

        if (execvp("tool", child_argv) != -1)
            _Exit(21);

        _Exit(errno == EACCES ? 0 : 22);
    }

    if (child > 0)
        CHECK(wait_for_zero(child) == 0);

    CHECK(unlink(noexec_tool) == 0);
    CHECK(unlink(bin_tool) == 0);
    CHECK(unlink(other_tool) == 0);
    CHECK(rmdir(noexec) == 0);
    CHECK(rmdir(bindir) == 0);
    CHECK(rmdir(other) == 0);

    if (fails)
        printf("execvp_test: %d failure(s)\n", fails);

    return fails != 0;
}
