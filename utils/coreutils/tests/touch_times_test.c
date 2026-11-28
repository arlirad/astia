#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int cu_touch_main(int argc, char** argv);
int cu_test_utimensat(int dirfd, const char* path, const struct timespec times[2],
                      int flags);

static int simulate_missing_omit;
static int rejected_omits;
static int failures;

int cu_test_utimensat(int dirfd, const char* path, const struct timespec times[2],
                      int flags) {
    if (simulate_missing_omit && times != NULL &&
        (times[0].tv_nsec == UTIME_OMIT || times[1].tv_nsec == UTIME_OMIT)) {
        ++rejected_omits;

        errno = EINVAL;

        return -1;
    }

    return utimensat(dirfd, path, times, flags);
}

static void expect(int condition, const char* what) {
    if (condition)
        return;

    printf("touch times test failed: %s\n", what);
    ++failures;
}

static void check_times(const char* path, int atime_updated, int mtime_updated) {
    struct stat info;

    if (stat(path, &info) != 0) {
        expect(0, "stat fixture");

        return;
    }

    expect((info.st_atime != 1000) == atime_updated, "atime selection");
    expect((info.st_mtime != 2000) == mtime_updated, "mtime selection");
}

static int create_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;

    return close(fd);
}

static int set_times(const char* path, time_t atime, time_t mtime) {
    struct timespec times[2] = {{atime, 0}, {mtime, 0}};

    return utimensat(AT_FDCWD, path, times, 0);
}

static int run_touch(char** argv) {
    int argc = 0;

    while (argv[argc] != NULL) {
        ++argc;
    }

    return cu_touch_main(argc, argv);
}

static int selective_checks(const char* path, char** access_argv, char** modify_argv) {
    if (create_file(path) != 0 || set_times(path, 1000, 2000) != 0)
        return 1;

    expect(run_touch(access_argv) == 0, "touch -a exit status");
    check_times(path, 1, 0);

    if (set_times(path, 1000, 2000) != 0)
        return 1;

    expect(run_touch(modify_argv) == 0, "touch -m exit status");
    check_times(path, 0, 1);

    return 0;
}

int main(void) {
    char directory[] = "/tmp/astia-touch-times-XXXXXX";
    char file[sizeof directory + 16];
    char created[sizeof directory + 16];

    char* touch_access[]  = {"touch", "-a", file, NULL};
    char* touch_modify[]  = {"touch", "-m", file, NULL};
    char* touch_both[]    = {"touch", "-a", "-m", file, NULL};
    char* touch_default[] = {"touch", file, NULL};
    char* touch_create[]  = {"touch", "-a", created, NULL};

    struct stat info;

    if (mkdtemp(directory) == NULL) {
        printf("touch times test failed: mkdtemp\n");

        return 1;
    }

    snprintf(file, sizeof file, "%s/file", directory);
    snprintf(created, sizeof created, "%s/created", directory);

    if (selective_checks(file, touch_access, touch_modify) != 0) {
        printf("touch times test failed: selective setup\n");

        return 1;
    }

    simulate_missing_omit = 1;
    rejected_omits        = 0;

    if (selective_checks(file, touch_access, touch_modify) != 0) {
        printf("touch times test failed: fallback setup\n");

        return 1;
    }

    expect(rejected_omits > 0, "fallback exercised UTIME_OMIT rejection");

    simulate_missing_omit = 0;

    if (set_times(file, 1000, 2000) != 0)
        return 1;

    expect(run_touch(touch_both) == 0, "touch -a -m exit status");
    check_times(file, 1, 1);

    if (set_times(file, 1000, 2000) != 0)
        return 1;

    expect(run_touch(touch_default) == 0, "touch exit status");
    check_times(file, 1, 1);

    expect(run_touch(touch_create) == 0, "touch -a create exit status");
    expect(stat(created, &info) == 0, "touch -a create stat");

    (void) unlink(file);
    (void) unlink(created);
    (void) rmdir(directory);

    return failures == 0 ? 0 : 1;
}
