/* Host regression for cross-filesystem mv of symbolic links.
 *
 * The host test drives cu_mv_main across /tmp and /dev/shm so the EXDEV
 * fallback runs against two real filesystems.  The in-guest suite covers the
 * same paths through the built binaries; this one also runs where /dev/shm is
 * absent by skipping without failing. */

#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_CAP 1024

typedef struct {
    const char* name;
    const char* target;
} symlink_case;

int cu_mv_main(int argc, char** argv);

static int fail(const char* what) {
    fprintf(stderr, "mv symlink test failed: %s\n", what);

    return 1;
}

static int fail_case(const char* name, const char* what) {
    fprintf(stderr, "mv symlink test failed: %s: %s\n", name, what);

    return 1;
}

static int is_symlink(const char* path) {
    struct stat info;

    return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
}

static int is_absent(const char* path) {
    struct stat info;

    return lstat(path, &info) != 0 && errno == ENOENT;
}

static int symlink_target_is(const char* path, const char* expected) {
    char buffer[TEST_PATH_CAP];

    long count = readlink(path, buffer, sizeof buffer - 1);
    if (count < 0)
        return 0;

    buffer[count] = '\0';

    return strcmp(buffer, expected) == 0;
}

static int file_equals(const char* path, const char* expected) {
    char buffer[64];

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

static int run_case(const char* source_dir, const char* destination_dir,
                    const symlink_case* test) {
    char source[TEST_PATH_CAP];
    char destination[TEST_PATH_CAP];

    snprintf(source, sizeof source, "%s/%s", source_dir, test->name);
    snprintf(destination, sizeof destination, "%s/%s", destination_dir, test->name);

    (void) unlink(source);
    (void) unlink(destination);

    if (symlink(test->target, source) != 0)
        return fail_case(test->name, "fixture symlink");

    char* argv[] = {"mv", source, destination, NULL};

    if (cu_mv_main(3, argv) != 0)
        return fail_case(test->name, "move failed");

    if (!is_symlink(destination))
        return fail_case(test->name, "destination is not a symlink");

    if (!symlink_target_is(destination, test->target))
        return fail_case(test->name, "destination target changed");

    if (!is_absent(source))
        return fail_case(test->name, "source was not removed");

    (void) unlink(destination);

    return 0;
}

int main(void) {
    char source_dir[]      = "/tmp/astia-mv-symlink-src-XXXXXX";
    char destination_dir[] = "/dev/shm/astia-mv-symlink-dst-XXXXXX";

    if (mkdtemp(source_dir) == NULL)
        return fail("mkdtemp source");

    if (mkdtemp(destination_dir) == NULL) {
        printf("mv symlink test skipped: no /dev/shm\n");
        rmdir(source_dir);

        return 0;
    }

    struct stat source_info;
    struct stat destination_info;

    int status = 0;

    if (stat(source_dir, &source_info) != 0 ||
        stat(destination_dir, &destination_info) != 0 ||
        source_info.st_dev == destination_info.st_dev) {
        printf("mv symlink test skipped: /tmp and /dev/shm share a filesystem\n");
        rmdir(destination_dir);
        rmdir(source_dir);

        return 0;
    }

    symlink_case cases[] = {
        {"relative", "target"},
        {"dangling", "/nonexistent/astia-669"},
    };

    char target_path[TEST_PATH_CAP];

    snprintf(target_path, sizeof target_path, "%s/target", source_dir);

    FILE* target_file = fopen(target_path, "w");
    if (target_file == NULL) {
        status = fail("fixture target file");
    }
    else {
        fputs("keep\n", target_file);
        fclose(target_file);
    }

    if (status == 0) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            if (run_case(source_dir, destination_dir, &cases[i]) != 0) {
                status = 1;

                break;
            }
        }
    }

    if (status == 0 && !file_equals(target_path, "keep\n"))
        status = fail("symlink target file was modified");

    (void) unlink(target_path);

    rmdir(destination_dir);
    rmdir(source_dir);

    return status;
}
