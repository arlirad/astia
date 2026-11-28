#define main rm_program_main
#include "../src/rm.c"
#undef main

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>

static int failures;

static void expect_root(const char* path) {
    if (!root_operand(path)) {
        fprintf(stderr, "expected root operand: %s\n", path);

        failures = 1;
    }
}

static void expect_not_root(const char* path) {
    if (root_operand(path)) {
        fprintf(stderr, "unexpected root operand: %s\n", path);

        failures = 1;
    }
}

static void expect_dot(const char* path) {
    if (!dot_operand(path)) {
        fprintf(stderr, "expected . or .. operand: %s\n", path);

        failures = 1;
    }
}

static void expect_not_dot(const char* path) {
    if (dot_operand(path)) {
        fprintf(stderr, "unexpected . or .. operand: %s\n", path);

        failures = 1;
    }
}

static int create_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    return close(fd);
}

int main(void) {
    expect_root("/");
    expect_root("//");
    expect_root("///");
    expect_root("/.");
    expect_root("/./");
    expect_root("/..");
    expect_root("/../");
    expect_root("/etc/..");
    expect_root("/tmp/../");
    expect_not_root("");
    expect_not_root("/tmp");
    expect_not_root("/tmp/./");

    expect_dot(".");
    expect_dot("..");
    expect_dot("./");
    expect_dot("x/.");
    expect_dot("x/..");
    expect_dot("/x/.");
    expect_dot("/x/../");
    expect_not_dot("");
    expect_not_dot("/");
    expect_not_dot("/tmp");

    char base[] = "/tmp/astia-rm-root-guard-XXXXXX";

    char* directory = mkdtemp(base);

    if (directory == 0)
        return 1;

    char link[PATH_MAX];
    char link_slash[PATH_MAX];
    char tree[PATH_MAX];
    char nested[PATH_MAX];
    char file[PATH_MAX];

    if (snprintf(link, sizeof link, "%s/rootlink", directory) >= (int) sizeof link ||
        snprintf(link_slash, sizeof link_slash, "%s/", link) >= (int) sizeof link_slash ||
        snprintf(tree, sizeof tree, "%s/tree", directory) >= (int) sizeof tree ||
        snprintf(nested, sizeof nested, "%s/nested", tree) >= (int) sizeof nested ||
        snprintf(file, sizeof file, "%s/file", nested) >= (int) sizeof file) {
        (void) rmdir(directory);

        return 1;
    }

    if (symlink("/", link) != 0) {
        (void) rmdir(directory);

        return 1;
    }

    expect_root(link);
    expect_root(link_slash);
    expect_not_root(directory);
    expect_not_dot(link);
    expect_not_dot(directory);

    if (mkdir(tree, 0755) != 0 || mkdir(nested, 0755) != 0 || create_file(file) != 0) {
        (void) unlink(link);
        (void) rmdir(directory);

        return 1;
    }

    if (remove_path(tree, 1) != 0) {
        fprintf(stderr, "recursive removal below the root failed: %s\n", tree);

        failures = 1;
    }

    struct stat info;

    if (lstat(tree, &info) == 0) {
        fprintf(stderr, "recursive removal left a directory behind: %s\n", tree);

        failures = 1;
    }

    (void) unlink(link);
    (void) rmdir(directory);

    return failures;
}
