#include "common.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char* child_path(const char* parent, const char* name) {
    size_t parent_len = strlen(parent);
    size_t name_len = strlen(name);
    int slash = parent_len != 0 && parent[parent_len - 1] != '/';
    char* result = malloc(parent_len + (size_t)slash + name_len + 1);
    if (result == 0) return 0;
    memcpy(result, parent, parent_len);
    if (slash) result[parent_len++] = '/';
    memcpy(result + parent_len, name, name_len + 1);
    return result;
}

static const char* base_name(const char* path) {
    const char* end = path + strlen(path);
    while (end != path && end[-1] == '/') --end;
    while (end != path && end[-1] != '/') --end;
    return end;
}

static int copy_fd(int input, int output, const char* name) {
    char buffer[4096];
    for (;;) {
        long count = read(input, buffer, sizeof buffer);
        if (count == 0)
            return 0;
        if (count < 0 || cu_write_all(output, buffer, (size_t)count) < 0)
            return cu_error("cp", name);
    }
}

static int copy_one(const char* source, const char* destination, mode_t mode);

static int copy_directory(const char* source, const char* destination, mode_t mode) {
    DIR* directory = opendir(source);
    struct dirent* entry;
    int status = 0;
    if (mkdir(destination, mode & 0777) < 0) {
        struct stat existing;
        if (errno != EEXIST || stat(destination, &existing) < 0 || !S_ISDIR(existing.st_mode))
            return cu_error("cp", destination);
    }
    if (directory == 0) return cu_error("cp", source);
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0) status = cu_error("cp", source);
            break;
        }
        char* from;
        char* to;
        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;
        from = child_path(source, entry->d_name);
        to = child_path(destination, entry->d_name);
        if (from == 0 || to == 0) {
            free(from); free(to);
            status = cu_error("cp", "out of memory");
            break;
        }
        if (copy_one(from, to, 0777) != 0) status = 1;
        free(from);
        free(to);
    }
    closedir(directory);
    return status;
}

static int copy_one(const char* source, const char* destination, mode_t mode) {
    struct stat info;
    int input;
    int output;
    int status;
    if (stat(source, &info) < 0) return cu_error("cp", source);
    if (S_ISDIR(info.st_mode)) return copy_directory(source, destination, info.st_mode);
    input = open(source, O_RDONLY);
    if (input < 0) return cu_error("cp", source);
    output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (output < 0) {
        close(input);
        return cu_error("cp", destination);
    }
    status = copy_fd(input, output, source);
    close(input);
    close(output);
    return status;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    const char* source = 0;
    const char* destination = 0;
    int recursive = 0;
    const char* value;
    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("cp", "[-r] SOURCE DEST", "Copy files or directories; - denotes standard input or output.");
        if (cu_is_version(value))
            return cu_version("cp");
        if (args.options && cu_arg_is(value, "-r", "--recursive")) {
            recursive = 1;
            continue;
        }
        if (args.options && value[0] == '-' && __builtin_strcmp(value, "-") != 0)
            return cu_error("cp", "unknown option");
        if (source == 0)
            source = value;
        else if (destination == 0)
            destination = value;
        else
            return cu_usage("cp", "SOURCE DEST");
    }
    if (source == 0 || destination == 0)
        return cu_usage("cp", "[-r] SOURCE DEST");

    struct stat source_info;
    int source_is_stdin = __builtin_strcmp(source, "-") == 0;
    if (!source_is_stdin && stat(source, &source_info) < 0)
        return cu_error("cp", source);
    if (!source_is_stdin && S_ISDIR(source_info.st_mode) && !recursive)
        return cu_error("cp", "omitting directory (use -r)");

    char* target = 0;
    const char* target_path = destination;
    struct stat destination_info;
    if (stat(destination, &destination_info) == 0 && S_ISDIR(destination_info.st_mode)) {
        target = child_path(destination, base_name(source));
        if (target == 0) return cu_error("cp", "out of memory");
        target_path = target;
    }

    if (__builtin_strcmp(source, "-") == 0 || __builtin_strcmp(destination, "-") == 0) {
        int input = CU_STDIN;
        int output = CU_STDOUT;
        if (__builtin_strcmp(source, "-") != 0) input = open(source, O_RDONLY);
        if (__builtin_strcmp(destination, "-") != 0) output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (input < 0 || output < 0) {
            if (input > CU_STDERR) close(input);
            if (output > CU_STDERR) close(output);
            free(target);
            return cu_error("cp", destination);
        }
        int status = copy_fd(input, output, source);
        if (input > CU_STDERR) close(input);
        if (output > CU_STDERR) close(output);
        free(target);
        return status;
    }
    int status = copy_one(source, target_path, 0666);
    free(target);
    return status;
}
