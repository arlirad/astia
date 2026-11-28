#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char* swap_path;
static const char* swap_target;
static int         swap_state;

static void rm_test_hook(const char* path) {
    if (swap_state != 0 || swap_path == 0 || strcmp(path, swap_path) != 0)
        return;

    swap_state = 1;

    if (rmdir(path) != 0)
        return;

    (void) symlink(swap_target, path);
}

#define ASTIA_RM_TEST_HOOK rm_test_hook
#define main rm_program_main
#include "../src/rm.c"
#undef main

static int failures;

static int create_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    return close(fd);
}

static int path_join(char* out, size_t capacity, const char* base, const char* name) {
    int written = snprintf(out, capacity, "%s/%s", base, name);

    return written < 0 || (size_t) written >= capacity ? -1 : 0;
}

static void expect_regular(const char* path) {
    struct stat info;

    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "expected a regular file: %s\n", path);

        failures = 1;
    }
}

static void expect_absent(const char* path) {
    struct stat info;

    if (lstat(path, &info) == 0) {
        fprintf(stderr, "expected path to be absent: %s\n", path);

        failures = 1;
    }
}

static void expect_symlink(const char* path) {
    struct stat info;

    if (lstat(path, &info) != 0 || !S_ISLNK(info.st_mode)) {
        fprintf(stderr, "expected a symlink: %s\n", path);

        failures = 1;
    }
}

static void swap_case(const char* base) {
    char tree[PATH_MAX];
    char victim[PATH_MAX];
    char keep[PATH_MAX];

    if (path_join(tree, sizeof tree, base, "swap-tree") < 0 ||
        path_join(victim, sizeof victim, base, "swap-victim") < 0 ||
        path_join(keep, sizeof keep, victim, "keep") < 0) {
        failures = 1;

        return;
    }

    if (mkdir(tree, 0755) != 0 || mkdir(victim, 0755) != 0 || create_file(keep) != 0) {
        failures = 1;

        return;
    }

    swap_path   = tree;
    swap_target = victim;
    swap_state  = 0;

    if (remove_path(tree, 1) == 0) {
        fprintf(stderr, "recursive removal followed the swapped-in symlink: %s\n", tree);

        failures = 1;
    }

    expect_symlink(tree);
    expect_regular(keep);

    swap_path = 0;

    (void) unlink(tree);
    (void) unlink(keep);
    (void) rmdir(victim);
}

static void nested_symlink_case(const char* base) {
    char tree[PATH_MAX];
    char nested[PATH_MAX];
    char link[PATH_MAX];
    char victim[PATH_MAX];
    char keep[PATH_MAX];

    if (path_join(tree, sizeof tree, base, "nested-tree") < 0 ||
        path_join(nested, sizeof nested, tree, "nested") < 0 ||
        path_join(link, sizeof link, nested, "link") < 0 ||
        path_join(victim, sizeof victim, base, "nested-victim") < 0 ||
        path_join(keep, sizeof keep, victim, "keep") < 0) {
        failures = 1;

        return;
    }

    if (mkdir(tree, 0755) != 0 || mkdir(nested, 0755) != 0 || mkdir(victim, 0755) != 0 ||
        create_file(keep) != 0 || symlink(victim, link) != 0) {
        failures = 1;

        return;
    }

    if (remove_path(tree, 1) != 0) {
        fprintf(stderr, "recursive removal refused a symlink child: %s\n", tree);

        failures = 1;
    }

    expect_absent(tree);
    expect_regular(keep);

    (void) unlink(keep);
    (void) rmdir(victim);
}

int main(void) {
    char base[] = "/tmp/astia-rm-symlink-swap-XXXXXX";

    char* directory = mkdtemp(base);

    if (directory == 0)
        return 1;

    swap_case(directory);
    nested_symlink_case(directory);

    (void) rmdir(directory);

    return failures;
}
