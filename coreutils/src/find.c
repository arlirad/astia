#include "common.h"
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

typedef enum { follow_never, follow_command_line, follow_always } follow_mode;

typedef struct ancestor {
    const char*            path;
    const struct ancestor* parent;
} ancestor;

static char* child_path(const char* parent, const char* name) {
    size_t parent_len = __builtin_strlen(parent);
    size_t name_len   = __builtin_strlen(name);
    int    slash      = parent_len != 0 && parent[parent_len - 1] != '/';

    char* result = malloc(parent_len + (size_t) slash + name_len + 1);
    if (result == 0)
        return 0;

    __builtin_memcpy(result, parent, parent_len);

    if (slash)
        result[parent_len++] = '/';

    __builtin_memcpy(result + parent_len, name, name_len + 1);

    return result;
}

static int loop_error(const char* path) {
    cu_puts_fd(CU_STDERR, "find: filesystem loop detected: ");
    cu_puts_fd(CU_STDERR, path);
    cu_puts_fd(CU_STDERR, "\n");

    return 1;
}

static int is_ancestor(const ancestor* parent, const char* path) {
    for (const ancestor* cursor = parent; cursor != 0; cursor = cursor->parent) {
        if (__builtin_strcmp(cursor->path, path) == 0)
            return 1;
    }

    return 0;
}

static int visit(const char* path, follow_mode follow, int depth,
                 const ancestor* parent) {
    struct stat    info;
    DIR*           directory;
    struct dirent* entry;
    char*          resolved;

    int follow_path =
        follow == follow_always || (follow == follow_command_line && depth == 0);
    int status = 0;

    if ((follow_path ? stat(path, &info) : lstat(path, &info)) < 0 || cu_puts(path) < 0 ||
        cu_puts("\n") < 0)
        return cu_error("find", path);

    if (!S_ISDIR(info.st_mode))
        return 0;

    resolved = 0;

    if (follow == follow_always) {
        resolved = realpath(path, 0);
        if (resolved == 0)
            return cu_error("find", path);

        if (is_ancestor(parent, resolved)) {
            free(resolved);

            return loop_error(path);
        }
    }

    ancestor current = {resolved, parent};

    directory = opendir(path);
    if (directory == 0) {
        free(resolved);

        return cu_error("find", path);
    }

    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0)
                status = cu_error("find", path);

            break;
        }

        char* child;

        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;

        child = child_path(path, entry->d_name);
        if (child == 0) {
            status = cu_error("find", "out of memory");

            break;
        }

        if (visit(child, follow, depth + 1, &current) != 0)
            status = 1;

        free(child);
    }

    closedir(directory);
    free(resolved);

    return status;
}

int main(int argc, char** argv) {
    cu_args     args;
    const char* path;
    follow_mode follow = follow_never;

    int operands = 0;
    int status   = 0;

    cu_args_init(&args, argc, argv);

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("find", "[-H] [-L] [-P] [PATH...]",
                           "Print each PATH and its descendants.\n"
                           "\n"
                           "  -H  follow command-line symbolic links\n"
                           "  -L  follow all symbolic links\n"
                           "  -P  never follow symbolic links (default)");

        if (cu_is_version(path))
            return cu_version("find");

        if (args.options && path[0] == '-') {
            if (__builtin_strcmp(path, "-H") == 0)
                follow = follow_command_line;
            else if (__builtin_strcmp(path, "-L") == 0)
                follow = follow_always;
            else if (__builtin_strcmp(path, "-P") == 0)
                follow = follow_never;
            else
                return cu_error("find", "unknown option");

            continue;
        }

        ++operands;

        if (visit(path, follow, 0, 0) != 0)
            status = 1;
    }

    if (operands == 0)
        status = visit(".", follow, 0, 0);

    return operands == 0 && status == 0 ? 0 : status;
}
