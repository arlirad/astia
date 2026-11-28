#include "common.h"
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#ifndef ASTIA_RM_TEST_HOOK
#define ASTIA_RM_TEST_HOOK(path) ((void) (path))
#endif

struct child_list {
    char** names;
    size_t count;
    size_t capacity;
};

static void free_children(struct child_list* children) {
    for (size_t index = 0; index < children->count; ++index) {
        free(children->names[index]);
    }

    free(children->names);

    children->names    = 0;
    children->count    = 0;
    children->capacity = 0;
}

static int append_child(struct child_list* children, const char* name) {
    if (children->count == children->capacity) {
        size_t next_capacity = children->capacity == 0 ? 8 : children->capacity * 2;

        char** grown = realloc(children->names, next_capacity * sizeof *grown);
        if (grown == 0)
            return -1;

        children->names    = grown;
        children->capacity = next_capacity;
    }

    size_t length = __builtin_strlen(name);

    char* copy = malloc(length + 1);
    if (copy == 0)
        return -1;

    __builtin_memcpy(copy, name, length + 1);

    children->names[children->count++] = copy;

    return 0;
}

static int collect_children(int fd, struct child_list* children) {
    int scan = dup(fd);
    if (scan < 0)
        return -1;

    DIR* directory = fdopendir(scan);
    if (directory == 0) {
        close(scan);

        return -1;
    }

    struct dirent* entry;

    int status = 0;

    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0)
                status = -1;

            break;
        }

        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;

        if (append_child(children, entry->d_name) < 0)
            status = -1;

        if (status < 0)
            break;
    }

    closedir(directory);

    return status;
}

static int same_identity(const struct stat* left, const struct stat* right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int open_directory_at(int parent_fd, const char* name,
                             const struct stat* expected) {
    int fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;

    struct stat opened;

    if (fstat(fd, &opened) < 0) {
        int saved = errno;

        close(fd);

        errno = saved;

        return -1;
    }

    if (!same_identity(&opened, expected)) {
        close(fd);

        errno = ELOOP;

        return -1;
    }

    return fd;
}

static int entry_identity(int dirfd, const char* name, const struct stat* expected) {
    struct stat info;

    if (fstatat(dirfd, name, &info, AT_SYMLINK_NOFOLLOW) < 0)
        return -1;

    return same_identity(&info, expected);
}

static int remove_directory(int fd);

static int remove_child(int dirfd, const char* name, const struct stat* info) {
    if (!S_ISDIR(info->st_mode))
        return unlinkat(dirfd, name, 0);

    int child_fd = open_directory_at(dirfd, name, info);
    if (child_fd < 0)
        return -1;

    int status = remove_directory(child_fd);
    int saved  = errno;

    close(child_fd);

    errno = saved;

    if (status < 0)
        return -1;

    int matched = entry_identity(dirfd, name, info);
    if (matched < 0)
        return -1;

    if (matched == 0) {
        errno = ELOOP;

        return -1;
    }

    return unlinkat(dirfd, name, AT_REMOVEDIR);
}

static int remove_directory(int fd) {
    struct child_list children = {0};

    if (collect_children(fd, &children) < 0) {
        free_children(&children);

        return -1;
    }

    int status = 0;

    for (size_t index = 0; index < children.count; ++index) {
        struct stat info;

        if (fstatat(fd, children.names[index], &info, AT_SYMLINK_NOFOLLOW) < 0 ||
            remove_child(fd, children.names[index], &info) < 0) {
            status = -1;

            break;
        }
    }

    free_children(&children);

    return status;
}

static int remove_path(const char* path, int recursive) {
    struct stat info;

    if (lstat(path, &info) < 0)
        return -1;

    if (!S_ISDIR(info.st_mode))
        return unlink(path);

    if (!recursive) {
        errno = EISDIR;

        return -1;
    }

    ASTIA_RM_TEST_HOOK(path);

    int fd = open_directory_at(AT_FDCWD, path, &info);
    if (fd < 0)
        return -1;

    int status = remove_directory(fd);
    int saved  = errno;

    close(fd);

    errno = saved;

    if (status < 0)
        return -1;

    return rmdir(path);
}

static size_t trim_trailing_slashes(const char* path) {
    size_t end = __builtin_strlen(path);

    while (end != 0 && path[end - 1] == '/') {
        --end;
    }

    return end;
}

static int dot_operand(const char* path) {
    size_t end = trim_trailing_slashes(path);
    if (end == 0)
        return 0;

    size_t start = end;

    while (start != 0 && path[start - 1] != '/') {
        --start;
    }

    return (end - start == 1 && path[start] == '.') ||
           (end - start == 2 && path[start] == '.' && path[start + 1] == '.');
}

static int root_operand(const char* path) {
    size_t end = trim_trailing_slashes(path);
    if (end == 0)
        return path[0] == '/';

    char* resolved = realpath(path, 0);
    if (resolved == 0)
        return 0;

    int is_root = __builtin_strcmp(resolved, "/") == 0;

    free(resolved);

    return is_root;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    int force     = 0;
    int recursive = 0;
    int status    = 0;
    int operands  = 0;

    const char* path;

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("rm", "[-f] [-r] FILE...",
                           "Remove files or directories recursively.");

        if (cu_is_version(path))
            return cu_version("rm");

        if (args.options && cu_arg_is(path, "-f", "--force")) {
            force = 1;

            continue;
        }

        if (args.options &&
            (cu_arg_is(path, "-r", "--recursive") || cu_arg_is(path, "-R", 0))) {
            recursive = 1;

            continue;
        }

        if (args.options && path[0] == '-')
            return cu_error("rm", "unknown option");

        ++operands;

        if (root_operand(path)) {
            status |= cu_error("rm", "refusing to remove the filesystem root");

            continue;
        }

        if (dot_operand(path)) {
            status |= cu_error("rm", "refusing to remove . or ..");

            continue;
        }

        if (remove_path(path, recursive) < 0 && !(force && errno == ENOENT))
            status |= cu_error("rm", path);
    }

    if (operands == 0 && !force)
        return cu_usage("rm", "[-f] FILE...");

    return status;
}
