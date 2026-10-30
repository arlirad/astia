#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

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

static int visit(const char* path) {
    struct stat info;
    DIR* directory;
    struct dirent* entry;
    int status = 0;
    if (stat(path, &info) < 0 || cu_puts(path) < 0 || cu_puts("\n") < 0)
        return cu_error("find", path);
    if (!S_ISDIR(info.st_mode)) return 0;
    directory = opendir(path);
    if (directory == 0) return cu_error("find", path);
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0) status = cu_error("find", path);
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
        if (visit(child) != 0) status = 1;
        free(child);
    }
    closedir(directory);
    return status;
}

int main(int argc, char** argv) {
    cu_args args;
    const char* path;
    int operands = 0;
    int status = 0;
    cu_args_init(&args, argc, argv);
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("find", "[PATH...]", "Print each PATH and its descendants.");
        if (cu_is_version(path)) return cu_version("find");
        if (args.options && path[0] == '-') return cu_error("find", "unknown option");
        ++operands;
        if (visit(path) != 0) status = 1;
    }
    if (operands == 0) status = visit(".");
    return operands == 0 && status == 0 ? 0 : status;
}
