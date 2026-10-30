#include "common.h"

#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

static char* child_path(const char* parent, const char* name) {
    size_t parent_len = __builtin_strlen(parent);
    size_t name_len = __builtin_strlen(name);
    int slash = parent_len != 0 && parent[parent_len - 1] != '/';
    char* result = malloc(parent_len + (size_t)slash + name_len + 1);
    if (result == 0) return 0;
    __builtin_memcpy(result, parent, parent_len);
    if (slash) result[parent_len++] = '/';
    __builtin_memcpy(result + parent_len, name, name_len + 1);
    return result;
}

static int remove_path(const char* path, int recursive) {
    struct stat info;
    if (stat(path, &info) < 0) return -1;
    if (!S_ISDIR(info.st_mode)) return unlink(path);
    if (!recursive) {
        errno = EISDIR;
        return -1;
    }
    DIR* directory = opendir(path);
    struct dirent* entry;
    int status = 0;
    if (directory == 0) return -1;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0) status = -1;
            break;
        }
        char* child;
        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;
        child = child_path(path, entry->d_name);
        if (child == 0 || remove_path(child, recursive) < 0) status = -1;
        free(child);
        if (status < 0) break;
    }
    closedir(directory);
    if (status < 0) return -1;
    return rmdir(path);
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int force = 0;
    int recursive = 0;
    int status = 0;
    int operands = 0;
    const char* path;
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("rm", "[-f] [-r] FILE...", "Remove files or directories recursively.");
        if (cu_is_version(path))
            return cu_version("rm");
        if (args.options && cu_arg_is(path, "-f", "--force")) {
            force = 1;
            continue;
        }
        if (args.options && (cu_arg_is(path, "-r", "--recursive") ||
                             cu_arg_is(path, "-R", 0))) {
            recursive = 1;
            continue;
        }
        if (args.options && path[0] == '-')
            return cu_error("rm", "unknown option");
        ++operands;
        if (remove_path(path, recursive) < 0 && !(force && errno == ENOENT))
            status |= cu_error("rm", path);
    }
    if (operands == 0)
        return cu_usage("rm", "[-f] FILE...");
    return status;
}
