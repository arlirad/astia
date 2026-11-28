/* Host regression for `ln TARGET DIR` (issue #819).
 *
 * The host test drives cu_ln_main inside a temporary directory so a directory
 * operand must resolve to DIR/<basename TARGET>, including the trailing-slash
 * form and relative symbolic-link targets.  The in-guest suite covers the same
 * paths through the built binary. */

#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_CAP 512

int cu_ln_main(int argc, char** argv);

static int fail(const char* what) {
    fprintf(stderr, "ln dir test failed: %s\n", what);

    return 1;
}

static int is_symlink(const char* path) {
    struct stat info;

    return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
}

static int same_file(const char* left, const char* right) {
    struct stat left_info;
    struct stat right_info;

    return stat(left, &left_info) == 0 && stat(right, &right_info) == 0 &&
           left_info.st_dev == right_info.st_dev && left_info.st_ino == right_info.st_ino;
}

static int symlink_target_is(const char* path, const char* expected) {
    char buffer[TEST_PATH_CAP];

    long count = readlink(path, buffer, sizeof buffer - 1);
    if (count < 0)
        return 0;

    buffer[count] = '\0';

    return strcmp(buffer, expected) == 0;
}

static int put_file(const char* path, const char* text) {
    FILE* file = fopen(path, "w");
    if (file == 0)
        return -1;

    int status = fwrite(text, 1, strlen(text), file) == strlen(text) ? 0 : -1;

    return fclose(file) == 0 ? status : -1;
}

static int invoke(char** argv) {
    int argc = 0;

    while (argv[argc] != 0) {
        ++argc;
    }

    return cu_ln_main(argc, argv);
}

static int make_path(char* buffer, const char* root, const char* name) {
    int written = snprintf(buffer, TEST_PATH_CAP, "%s/%s", root, name);

    return written > 0 && written < TEST_PATH_CAP ? 0 : -1;
}

static int run_checks(const char* root) {
    char target[TEST_PATH_CAP];
    char other[TEST_PATH_CAP];
    char directory[TEST_PATH_CAP];
    char link_directory[TEST_PATH_CAP];
    char joined[TEST_PATH_CAP];
    char plain[TEST_PATH_CAP];

    if (make_path(target, root, "target") != 0 || make_path(other, root, "other") != 0 ||
        make_path(directory, root, "dir") != 0 ||
        make_path(link_directory, root, "linkdir") != 0 ||
        make_path(joined, root, "dir/target") != 0 ||
        make_path(plain, root, "plain") != 0)
        return fail("path setup");

    char* ln_into_dir[]                = {"ln", "target", "dir", NULL};
    char* ln_into_dir_slash[]          = {"ln", "other", "dir/", NULL};
    char* ln_symbolic_into_dir[]       = {"ln", "-s", "target", "linkdir", NULL};
    char* ln_force_symbolic_into_dir[] = {"ln", "-sf", "other", "dir", NULL};
    char* ln_force_into_dir[]          = {"ln", "-f", "other", "dir", NULL};
    char* ln_plain[]                   = {"ln", "target", "plain", NULL};

    if (put_file(target, "target\n") != 0 || put_file(other, "other\n") != 0 ||
        mkdir(directory, 0777) != 0 || mkdir(link_directory, 0777) != 0)
        return fail("fixture setup");

    if (invoke(ln_into_dir) != 0 || !same_file(target, joined) || is_symlink(joined))
        return fail("ln file dir");

    if (make_path(joined, root, "dir/other") != 0)
        return fail("path setup");

    if (invoke(ln_into_dir_slash) != 0 || !same_file(other, joined))
        return fail("ln file dir/");

    if (make_path(joined, root, "dir/target") != 0)
        return fail("path reset");

    if (invoke(ln_into_dir) == 0)
        return fail("ln file dir existing name");

    if (!same_file(target, joined) || is_symlink(joined))
        return fail("ln file dir existing name kept");

    if (make_path(joined, root, "linkdir/target") != 0)
        return fail("path setup");

    if (invoke(ln_symbolic_into_dir) != 0 || !is_symlink(joined) ||
        !symlink_target_is(joined, "target"))
        return fail("ln -s relative target dir");

    if (make_path(joined, root, "dir/other") != 0)
        return fail("path reset");

    if (invoke(ln_force_symbolic_into_dir) != 0 || !is_symlink(joined) ||
        !symlink_target_is(joined, "other"))
        return fail("ln -sf file dir");

    if (invoke(ln_force_into_dir) != 0 || is_symlink(joined) || !same_file(other, joined))
        return fail("ln -f file dir");

    if (invoke(ln_plain) != 0 || !same_file(target, plain))
        return fail("ln target link");

    return 0;
}

static void remove_fixtures(const char* root) {
    static const char* const names[] = {"plain",          "dir/target", "dir/other",
                                        "linkdir/target", "dir",        "linkdir",
                                        "target",         "other"};

    char path[TEST_PATH_CAP];

    for (size_t index = 0; index < sizeof names / sizeof names[0]; ++index) {
        if (make_path(path, root, names[index]) != 0)
            continue;

        if (unlink(path) != 0 && errno != ENOENT)
            rmdir(path);
    }
}

int main(void) {
    char root[TEST_PATH_CAP] = "/tmp/astia-ln-dir-XXXXXX";

    if (mkdtemp(root) == 0)
        return fail("mkdtemp");

    if (chdir(root) != 0) {
        rmdir(root);

        return fail("chdir");
    }

    int status = run_checks(root);

    remove_fixtures(root);

    if (chdir("/") != 0)
        status = 1;

    if (rmdir(root) != 0)
        status = 1;

    return status;
}
