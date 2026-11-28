/* Host regression for a cross-filesystem mv onto a destination symlink.
 *
 * rename() replaces the destination entry itself, so the EXDEV fallback must
 * unlink the link instead of writing through it.  The test drives cu_mv_main
 * across /tmp and /dev/shm so the fallback runs between two real filesystems,
 * and skips without failing where /dev/shm is absent or not separate. */

#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_CAP 1024

int cu_mv_main(int argc, char** argv);

typedef struct {
    const char* name;
    const char* link_target;
    int         target_exists;
} destination_case;

static int fail(const char* name, const char* what) {
    fprintf(stderr, "mv destination symlink test failed: %s: %s\n", name, what);

    return 1;
}

static int is_regular_file(const char* path) {
    struct stat info;

    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int is_absent(const char* path) {
    struct stat info;

    return lstat(path, &info) != 0 && errno == ENOENT;
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

static int write_file(const char* path, const char* text) {
    FILE* file = fopen(path, "w");
    if (file == NULL)
        return 1;

    int failed = fputs(text, file) < 0;

    if (fclose(file) != 0)
        failed = 1;

    return failed;
}

static int mode_of(const char* path) {
    struct stat info;

    if (lstat(path, &info) != 0)
        return -1;

    return (int) (info.st_mode & 07777);
}

static int run_case(const char* source_dir, const char* destination_dir,
                    const destination_case* test) {
    char source[TEST_PATH_CAP];
    char destination[TEST_PATH_CAP];
    char target[TEST_PATH_CAP];

    snprintf(source, sizeof source, "%s/%s", source_dir, test->name);
    snprintf(destination, sizeof destination, "%s/%s", destination_dir, test->name);
    snprintf(target, sizeof target, "%s/%s", destination_dir, test->link_target);

    (void) unlink(source);
    (void) unlink(destination);
    (void) unlink(target);

    if (test->target_exists && write_file(target, "target\n") != 0)
        return fail(test->name, "fixture target file");

    if (symlink(test->link_target, destination) != 0)
        return fail(test->name, "fixture destination symlink");

    if (write_file(source, "source\n") != 0)
        return fail(test->name, "fixture source file");

    if (chmod(source, 0640) != 0)
        return fail(test->name, "fixture source mode");

    char* argv[] = {"mv", source, destination, NULL};

    int status = 0;

    if (cu_mv_main(3, argv) != 0)
        status = fail(test->name, "move failed");
    else if (!is_regular_file(destination))
        status = fail(test->name, "destination is not a regular file");
    else if (!file_equals(destination, "source\n"))
        status = fail(test->name, "destination content is not the source");
    else if (mode_of(destination) != 0640)
        status = fail(test->name, "destination mode changed");
    else if (!is_absent(source))
        status = fail(test->name, "source was not removed");

    if (status == 0 && test->target_exists && !file_equals(target, "target\n"))
        status = fail(test->name, "symlink target file was modified");

    if (status == 0 && !test->target_exists && !is_absent(target))
        status = fail(test->name, "content was created through a dangling link");

    (void) unlink(source);
    (void) unlink(destination);
    (void) unlink(target);

    return status;
}

int main(void) {
    char source_dir[]      = "/tmp/astia-mv-destlink-src-XXXXXX";
    char destination_dir[] = "/dev/shm/astia-mv-destlink-dst-XXXXXX";

    if (mkdtemp(source_dir) == NULL)
        return fail("setup", "mkdtemp source");

    if (mkdtemp(destination_dir) == NULL) {
        printf("mv destination symlink test skipped: no /dev/shm\n");
        rmdir(source_dir);

        return 0;
    }

    struct stat source_info;
    struct stat destination_info;

    int status = 0;

    if (stat(source_dir, &source_info) != 0 ||
        stat(destination_dir, &destination_info) != 0 ||
        source_info.st_dev == destination_info.st_dev) {
        printf("mv destination symlink test skipped: /tmp and /dev/shm share a "
               "filesystem\n");
        rmdir(destination_dir);
        rmdir(source_dir);

        return 0;
    }

    destination_case cases[] = {
        {"existing", "target", 1},
        {"dangling", "missing", 0},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (run_case(source_dir, destination_dir, &cases[i]) != 0) {
            status = 1;

            break;
        }
    }

    rmdir(destination_dir);
    rmdir(source_dir);

    return status;
}
